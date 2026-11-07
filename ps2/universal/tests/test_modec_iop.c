/* Execute bbpad's real setup/session/hook code, not a second implementation. */
#include <string.h>
#include <stdlib.h>
#define BBPAD_HOST_TEST
#include "../src/bbpad.c"

static unsigned checks, failures, now_ms, session, transfers, begins, delays;
static unsigned analog, config, mapped, injected_error, config_fail;
static unsigned lamp_mask, lamp_commits, prepared, prepared_ms, prepared_level;
static unsigned observed_small, observed_large;
static unsigned command_count[256];
static unsigned native_small, native_large;
#define CHECK(x) do { checks++; if (!(x)) { failures++; printf("FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); } } while(0)

static int DelayThread(int usec) { CHECK(!session); delays++; now_ms += usec/1000; return 0; }
static int GetSystemTime(iop_sys_clock_t *t) { t->lo = now_ms; t->hi = 0; return 0; }
static void SysClock2USec(iop_sys_clock_t *t, u32 *sec, u32 *usec)
{ *sec = t->lo/1000; *usec = (t->lo%1000)*1000; }
static void begin(void) { CHECK(!session); session = 1; begins++; }
static void end(void) { CHECK(session); session = 0; }

static int transfer(sio2_transfer_data_t *td)
{
    unsigned a = 0, b = 0, i;
    CHECK(session);
    transfers++;
    memset(td->out,0xFF,td->out_size);
    td->stat6c = injected_error ? 1u<<13 : 0;
    for (i = 0; i < 16 && td->regdata[i]; i++) {
        unsigned il = (td->regdata[i]>>8)&511, ol = (td->regdata[i]>>18)&511;
        u8 *in = td->in+a, *out = td->out+b;
        CHECK(a+il <= td->in_size && b+ol <= td->out_size);
        if ((td->regdata[i]&3) == 0 && il >= 5 && ol >= 5 && in[0] == 1) {
            command_count[in[1]]++;
            out[1] = config ? 0xF3 : analog == 2 ? 0x79 : analog ? 0x73 : 0x41;
            out[2] = 0x5A;
            if (in[1] == 0x43) config = in[3];
            if (in[1] == 0x44) { CHECK(config); analog = in[3]; if (config_fail) out[2] = 0; }
            if (in[1] == 0x4D) { CHECK(config); mapped = !in[3] && in[4] == 1; }
            if (in[1] == 0x42) {
                out[3] = 0xFE; out[4] = 0x7F; /* bits 0 + 15 */
                if (ol >= 9) { out[5] = 0xE0; out[6] = 0x80; out[7] = 0x80; out[8] = 0xE0; }
                observed_small = in[3]; observed_large = in[4];
                /* Stock netemu can suppress redundant both-zero motor states.
                 * In particular the first zero PREPARE may never reach USB. */
                if (mapped && !config && !injected_error &&
                    (in[3] || in[4] || native_small || native_large)) {
                    if (!in[3]) { prepared = 1; prepared_ms = now_ms; prepared_level = in[4]; }
                    else if (in[3] == 1 && prepared && prepared_level == in[4] && now_ms-prepared_ms <= 500) {
                        lamp_mask = in[4]/17; lamp_commits++; prepared = 0;
                    }
                }
                native_small = in[3]; native_large = in[4];
            }
        }
        a += il; b += ol;
    }
    return 1;
}

static void service(unsigned mask)
{
    bbpad_modec_lamps(mask);
    modec_session(0);
    CHECK(!session);
    now_ms += 16;
}

int main(void)
{
    unsigned i, base;
    u8 in[9] = {1,0x42,0,0x99,0x88,0,0,0,0}, out[9], original[9];
    sio2_transfer_data_t game = {0};
    real_begin = begin; real_end = end; real_a = real_b = transfer;
    bbpad_configure(14,BB_FLAG_POLL_AUTO);
    CHECK(bbpad_modec_enabled());
    /* Game never enables analog: setup must do it, atomically, then read. */
    lamp_mask = 15; /* e.g. a pre-existing local lamp policy owns the lights */
    service(0);
    CHECK(analog && mapped && !config && modec_ready);
    CHECK(begins == 1 && transfers == 5 && delays == 0);
    CHECK(bbpad_modec_sample() == ((1u<<0)|(1u<<15)|(1u<<17)|(1u<<18)));
    CHECK(!lamp_commits);
    service(0); service(0); service(0);
    CHECK(!lamp_commits && lamp_mask == 15); /* initial zero PREPARE was suppressed */
    for (i = 0; i < 17; i++) service(0);
    CHECK(lamp_commits >= 1 && lamp_mask == 0); /* periodic zero pair recovers */
    lamp_commits = 0;
    service(5); CHECK(!lamp_commits);
    service(5); CHECK(!lamp_commits);
    service(5); CHECK(lamp_commits == 1 && lamp_mask == 5);
    service(5); CHECK(lamp_commits == 1); /* repeated COMMIT ignored by Pico */
    for (i = 0; i < 36; i++) service(5);
    CHECK(lamp_commits >= 3 && lamp_mask == 5); /* unchanged lease refresh */
    /* PADMAN poll sees its exact input buffer restored and normal reply bytes. */
    game.regdata[0] = 0x240940; game.in = in; game.out = out;
    game.in_size = game.out_size = 9;
    memcpy(original,in,9);
    begin(); CHECK(hook_b(&game) == 1); end();
    CHECK(!memcmp(in,original,9) && out[1] == 0x73 && out[3] == 0xFE);
    CHECK(observed_small == (modec_motors&255) && observed_large == (modec_motors>>8));
    game.regdata[0] |= 1;
    begin(); hook_a(&game); end(); CHECK(!memcmp(in,original,9));
    game.regdata[0] &= ~3u;
    service(0); service(0); service(0); service(0);
    CHECK(!lamp_mask);
    base = lamp_commits;
    for (i = 0; i < 40; i++) service(0);
    CHECK(lamp_commits > base); /* zero also refreshes, making clear reliable */
    /* A digital reply and status error must not masquerade as twenty buttons. */
    injected_error = 1; service(9);
    CHECK(!bbpad_modec_sample() && !modec_ready);
    injected_error = 0;
    analog = 2; /* game-enabled pressure mode must survive motor-map setup */
    base = command_count[0x44];
    now_ms += 1000; service(9); CHECK(modec_ready);
    CHECK(analog == 2 && command_count[0x44] == base);
    service(9); service(9); CHECK(lamp_mask == 9);
    /* Game configuration is respected for 500 ms, then safely restored. */
    in[1] = 0x43; in[3] = 1;
    begin(); hook_b(&game); end();
    CHECK(!modec_ready && config);
    base = transfers; service(9); CHECK(transfers == base);
    now_ms += 1000; service(9); CHECK(!config && modec_ready);
    /* Failed setup ALWAYS exits config and is rate limited (no busy loop). */
    modec_ready = 0; analog = 0; now_ms += 1000; config_fail = 1;
    service(3); CHECK(!config && !modec_ready);
    base = transfers; service(3); CHECK(transfers == base);
    config_fail = 0; now_ms += 1000; service(3); CHECK(modec_ready);
    /* Game's later 5-byte poll cannot inject its own rumble on the reserved port. */
    in[1] = 0x42; in[3] = 0x99; in[4] = 0x88;
    game.in_size = game.out_size = 5; game.regdata[0] = 0x140540;
    memcpy(original,in,9);
    begin(); hook_b(&game); end(); CHECK(!memcmp(in,original,9));
    CHECK(bbpad_modec_sample() != 0); /* short game poll did not erase own axes */
    /* Other layouts never configure/overlay the reserved motor protocol. */
    bbpad_configure(0,BB_FLAG_POLL_AUTO);
    CHECK(!bbpad_modec_enabled());
    begin(); hook_b(&game); end(); CHECK(observed_small == 0x99 && observed_large == 0x88);
    printf("Mode C IOP: %u checks, %u failures\n",checks,failures);
    return failures ? 1 : 0;
}

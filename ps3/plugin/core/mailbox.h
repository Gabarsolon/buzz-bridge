/* SPDX-License-Identifier: MIT. Proposed byte ABI; no pointers cross the boundary. */
#ifndef BUZZ_MAILBOX_H
#define BUZZ_MAILBOX_H
#include "buzz.h"
#define BUZZ_MAILBOX_SIZE 32
void buzz_mailbox_request(const buzz_hub *h,uint8_t out[32]);
int buzz_mailbox_apply_lights(buzz_hub *h,const uint8_t reply[32]);
int buzz_mailbox_decode_reports(buzz_hub *h,const uint8_t request[32]);
void buzz_mailbox_reply(const buzz_hub *h,int ready,uint32_t generation,uint8_t reply[32]);
#endif

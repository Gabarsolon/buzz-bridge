/* int bb_call3(void *fn, void *gp, int a, int b, int c)
 *
 * Calls fn(a, b, c) with $gp = gp and restores the caller's $gp afterwards. Written in assembly
 * because a compiler is free to move a "restore gp" into the branch delay slot of the call, which
 * would run the client's callback with the wrong $gp (seen with gcc 15 in the first build). */
        .set    noreorder
        .set    noat
        .text
        .align  2
        .globl  bb_call3
        .ent    bb_call3
bb_call3:
        addiu   $sp, $sp, -24
        sw      $ra, 20($sp)
        sw      $gp, 16($sp)
        move    $t0, $a0          # fn
        move    $gp, $a1          # client's gp
        move    $a0, $a2          # a
        move    $a1, $a3          # b
        lw      $a2, 40($sp)      # c = fifth argument, passed on the caller's stack
        jalr    $t0
        nop
        lw      $gp, 16($sp)
        lw      $ra, 20($sp)
        jr      $ra
        addiu   $sp, $sp, 24
        .end    bb_call3

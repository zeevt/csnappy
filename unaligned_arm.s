# Written by http://stackoverflow.com/users/104109/bitbank
# Licensed under 3-clause BSD with permission. 
.syntax unified
.arch armv5te
.text
.align 2
.global get_unaligned_le_armv5
.arm
.type get_unaligned_le_armv5, %function
# When called from C, r0 = first parameter, r1 = second parameter
# r0-r3 and r12 can get trashed by C functions
get_unaligned_le_armv5:
.fnstart
  ldrb   %r2, [%r0],#1  		@ byte 0 is always read (n=1..4)
  cmp    %r1, #2
  ldrbge %r3, [%r0],#1  		@ byte 1, n == 2
  ldrbgt %r12,[%r0],#1  		@ byte 2, n > 2
  orrge  %r2, %r2,  %r3, LSL #8
  orrgt  %r2, %r2,  %r12,LSL #16
  cmp    %r1, #4
  ldrbeq %r3, [%r0],#1   		@ byte 3, n == 4
  movne  %r0, %r2               @ recoup wasted cycle
  orreq  %r0, %r2,  %r3, LSL #24
  bx lr
.fnend

.intel_syntax noprefix

.global custom_entry

.equ message_len, message_end - message_start

.data
message_start:
.asciz "Hello World!\n"
message_end:

.text
custom_entry:
  mov rax, 1 # write
  mov rdi, 1 # fd
  mov rsi, offset message_start # buf
  mov rdx, offset message_len # count
  syscall
  mov rax, 60 # exit
  mov rdi, 13 # status
  syscall
.end

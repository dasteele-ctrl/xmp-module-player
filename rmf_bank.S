.section .rodata
.balign 4
.global rmf_bank, rmf_bank_end
rmf_bank:
.incbin "patches111.hsb"
rmf_bank_end:
.section .note.GNU-stack,"",@progbits

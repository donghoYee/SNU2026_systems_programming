	.file	"decomment.c"
	.text
	.section	.rodata
	.align 8
.LC0:
	.string	"Error: line %d: unterminated comment\n"
	.text
	.globl	main
	.type	main, @function
main:
.LFB0:
	.cfi_startproc
	endbr64
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	subq	$32, %rsp
	movl	$0, -8(%rbp)
	movl	$1, -16(%rbp)
	movl	$-1, -12(%rbp)
.L37:
	call	getchar@PLT
	movl	%eax, -4(%rbp)
	cmpl	$-1, -4(%rbp)
	je	.L43
	movl	-4(%rbp), %eax
	movb	%al, -17(%rbp)
	cmpl	$8, -8(%rbp)
	ja	.L4
	movl	-8(%rbp), %eax
	leaq	0(,%rax,4), %rdx
	leaq	.L6(%rip), %rax
	movl	(%rdx,%rax), %eax
	cltq
	leaq	.L6(%rip), %rdx
	addq	%rdx, %rax
	notrack jmp	*%rax
	.section	.rodata
	.align 4
	.align 4
.L6:
	.long	.L14-.L6
	.long	.L13-.L6
	.long	.L12-.L6
	.long	.L11-.L6
	.long	.L10-.L6
	.long	.L9-.L6
	.long	.L8-.L6
	.long	.L7-.L6
	.long	.L5-.L6
	.text
.L14:
	cmpb	$47, -17(%rbp)
	jne	.L15
	movl	$1, -8(%rbp)
	jmp	.L4
.L15:
	cmpb	$34, -17(%rbp)
	jne	.L17
	movsbl	-17(%rbp), %eax
	movl	%eax, %edi
	call	putchar@PLT
	movl	$5, -8(%rbp)
	jmp	.L4
.L17:
	cmpb	$39, -17(%rbp)
	jne	.L18
	movsbl	-17(%rbp), %eax
	movl	%eax, %edi
	call	putchar@PLT
	movl	$7, -8(%rbp)
	jmp	.L4
.L18:
	movsbl	-17(%rbp), %eax
	movl	%eax, %edi
	call	putchar@PLT
	jmp	.L4
.L13:
	cmpb	$42, -17(%rbp)
	jne	.L19
	movl	$32, %edi
	call	putchar@PLT
	movl	-16(%rbp), %eax
	movl	%eax, -12(%rbp)
	movl	$2, -8(%rbp)
	jmp	.L4
.L19:
	cmpb	$47, -17(%rbp)
	jne	.L21
	movl	$32, %edi
	call	putchar@PLT
	movl	$4, -8(%rbp)
	jmp	.L4
.L21:
	cmpb	$34, -17(%rbp)
	jne	.L22
	movl	$47, %edi
	call	putchar@PLT
	movsbl	-17(%rbp), %eax
	movl	%eax, %edi
	call	putchar@PLT
	movl	$5, -8(%rbp)
	jmp	.L4
.L22:
	cmpb	$39, -17(%rbp)
	jne	.L23
	movl	$47, %edi
	call	putchar@PLT
	movsbl	-17(%rbp), %eax
	movl	%eax, %edi
	call	putchar@PLT
	movl	$7, -8(%rbp)
	jmp	.L4
.L23:
	movl	$47, %edi
	call	putchar@PLT
	movsbl	-17(%rbp), %eax
	movl	%eax, %edi
	call	putchar@PLT
	movl	$0, -8(%rbp)
	jmp	.L4
.L12:
	cmpb	$42, -17(%rbp)
	jne	.L24
	movl	$3, -8(%rbp)
	jmp	.L44
.L24:
	cmpb	$10, -17(%rbp)
	jne	.L44
	movl	$10, %edi
	call	putchar@PLT
	jmp	.L44
.L11:
	cmpb	$47, -17(%rbp)
	jne	.L26
	movl	$0, -8(%rbp)
	jmp	.L45
.L26:
	cmpb	$42, -17(%rbp)
	je	.L45
	cmpb	$10, -17(%rbp)
	jne	.L28
	movl	$10, %edi
	call	putchar@PLT
	movl	$2, -8(%rbp)
	jmp	.L45
.L28:
	movl	$2, -8(%rbp)
	jmp	.L45
.L10:
	cmpb	$10, -17(%rbp)
	jne	.L46
	movl	$10, %edi
	call	putchar@PLT
	movl	$0, -8(%rbp)
	jmp	.L46
.L9:
	cmpb	$34, -17(%rbp)
	jne	.L30
	movsbl	-17(%rbp), %eax
	movl	%eax, %edi
	call	putchar@PLT
	movl	$0, -8(%rbp)
	jmp	.L4
.L30:
	cmpb	$92, -17(%rbp)
	jne	.L32
	movsbl	-17(%rbp), %eax
	movl	%eax, %edi
	call	putchar@PLT
	movl	$6, -8(%rbp)
	jmp	.L4
.L32:
	movsbl	-17(%rbp), %eax
	movl	%eax, %edi
	call	putchar@PLT
	jmp	.L4
.L8:
	movsbl	-17(%rbp), %eax
	movl	%eax, %edi
	call	putchar@PLT
	movl	$5, -8(%rbp)
	jmp	.L4
.L7:
	cmpb	$39, -17(%rbp)
	jne	.L33
	movsbl	-17(%rbp), %eax
	movl	%eax, %edi
	call	putchar@PLT
	movl	$0, -8(%rbp)
	jmp	.L4
.L33:
	cmpb	$92, -17(%rbp)
	jne	.L35
	movsbl	-17(%rbp), %eax
	movl	%eax, %edi
	call	putchar@PLT
	movl	$8, -8(%rbp)
	jmp	.L4
.L35:
	movsbl	-17(%rbp), %eax
	movl	%eax, %edi
	call	putchar@PLT
	jmp	.L4
.L5:
	movsbl	-17(%rbp), %eax
	movl	%eax, %edi
	call	putchar@PLT
	movl	$7, -8(%rbp)
	jmp	.L4
.L44:
	nop
	jmp	.L4
.L45:
	nop
	jmp	.L4
.L46:
	nop
.L4:
	cmpb	$10, -17(%rbp)
	jne	.L37
	addl	$1, -16(%rbp)
	jmp	.L37
.L43:
	nop
	cmpl	$1, -8(%rbp)
	jne	.L38
	movl	$47, %edi
	call	putchar@PLT
	jmp	.L39
.L38:
	cmpl	$2, -8(%rbp)
	je	.L40
	cmpl	$3, -8(%rbp)
	jne	.L39
.L40:
	movq	stderr(%rip), %rax
	movl	-12(%rbp), %edx
	leaq	.LC0(%rip), %rcx
	movq	%rcx, %rsi
	movq	%rax, %rdi
	movl	$0, %eax
	call	fprintf@PLT
	movl	$1, %eax
	jmp	.L41
.L39:
	movl	$0, %eax
.L41:
	leave
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
.LFE0:
	.size	main, .-main
	.ident	"GCC: (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0"
	.section	.note.GNU-stack,"",@progbits
	.section	.note.gnu.property,"a"
	.align 8
	.long	1f - 0f
	.long	4f - 1f
	.long	5
0:
	.string	"GNU"
1:
	.align 8
	.long	0xc0000002
	.long	3f - 2f
2:
	.long	0x3
3:
	.align 8
4:

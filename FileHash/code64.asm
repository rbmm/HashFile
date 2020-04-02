.code

?__scasd@NT@@YA_NKPEAKK@Z proc
	xchg rdi,rdx
	mov eax,r8d
	repe scasd
	sete al
	mov rdi,rdx
	ret
?__scasd@NT@@YA_NKPEAKK@Z endp

end
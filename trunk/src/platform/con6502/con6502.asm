_EOL_CHAR equ 10

_adr_putchr .proc
;Write byte to address specified by _arr and increment the address by 1.
		 	
	;write the char to specified address	
	ldy #0
	sta (_arr),y

	inc _arr
	bne skip1
	inc _arr+1
skip1
	rts

.endp

_out_putchr .proc
;Write character from A to output.

		dta b($ff)
		rts

.endp


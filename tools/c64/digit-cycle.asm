; Simple C64 Digit Cycling Display
; Cycles all characters on screen through digits 0 to 9
; All characters are swapped in the frame border

; BASIC Loader: 10 SYS <start_address>
*=$0801
basic_stub:
        .word basic_end         ; Next line pointer
        .word 10                ; Line number 10
        .byte $9e               ; SYS token
        .byte " "
        .text format("%4d", start)  ; Auto-calculated SYS address
        .byte 0                 ; End of BASIC line
basic_end:
        .word 0                 ; End of BASIC program

start:
        sei                     ; Disable interrupts

        ; Set color RAM to light blue foreground ($0e)
        ldx #0
        lda #$0e
color_loop:
        sta $d800,x             ; Color RAM page 1
        sta $d900,x             ; Color RAM page 2
        sta $da00,x             ; Color RAM page 3
        sta $db00,x             ; Color RAM page 4 (partial)
        inx
        bne color_loop

        ; Initialize digit variable to $30 (ASCII '0')
        lda #$30
        sta digit

main_loop:
        ; Wait for raster line $ff
wait_raster:
        lda $d012
        cmp #$ff
        bne wait_raster

        ; Set all characters of display to be the value of digit variable
        ldx #0
        lda digit
fill_screen:
        sta $0400,x             ; Screen RAM page 1
        sta $0500,x             ; Screen RAM page 2
        sta $0600,x             ; Screen RAM page 3
        sta $0700,x             ; Screen RAM page 4 (partial)
        inx
        bne fill_screen

        ; Increment digit variable. If > $39, set to $30
        inc digit
        lda digit
        cmp #$3a                ; Compare with ASCII '9' + 1
        bne main_loop
        lda #$30                ; Reset to ASCII '0'
        sta digit
        jmp main_loop

; Variables
digit: .byte $30

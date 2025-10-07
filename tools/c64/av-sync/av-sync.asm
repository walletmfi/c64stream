; av-sync.asm - C64 Audio/Video Sync Test
; Press SPACE to flash border/background and play a tone
; Assemble with: 64tass av-sync.asm -o av-sync.prg

*=$0801
basic_stub:
        .word basic_end         ; pointer to next line
        .word 10                ; line number 10
        .byte $9e               ; SYS token
        .byte " "
        .text format("%4d", start)  ; SYS address auto-calculated
        .byte 0                 ; end of BASIC line
basic_end:
        .word 0                 ; end of BASIC program

start:
        sei                     ; disable interrupts

        lda #$0e                ; text color purple
        sta $d800               ; set first color RAM
        sta $d900
        sta $da00
        sta $db00

        lda #$00
        sta $d020               ; border black
        sta $d021               ; background black

main_loop:
        jsr wait_space
        jsr key_pressed_loop
        jmp main_loop

;--------------------------
; Wait until SPACE is pressed
;--------------------------
wait_space:
        lda $dc01               ; read CIA#1 keyboard port A
        and #%00010000          ; mask column for SPACE
        cmp #%00000000          ; active low (0 = pressed)
        bne wait_space
        rts

;--------------------------
; While key pressed: flash border/bg and play tone
;--------------------------
key_pressed_loop:
        lda #$06
        sta $d020               ; border blue
        lda #$06
        sta $d021               ; background blue

        lda #$0F
        sta $d418               ; SID volume max

        lda #$10
        sta $d400               ; frequency low byte
        lda #$11
        sta $d401               ; frequency high byte

        lda #%00010001           ; sawtooth waveform + gate on
        sta $d404                ; control register for voice 1

check_key:
        lda $dc01
        and #%00010000
        cmp #%00000000
        beq check_key            ; loop while space held

        ; key released: stop sound and restore colors
        lda #$00
        sta $d404                ; turn off SID gate
        lda #$00
        sta $d020                ; reset border to black
        lda #$0e
        sta $d021                ; restore background to purple
        rts

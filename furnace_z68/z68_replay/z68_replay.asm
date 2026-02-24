; =======================================================
; X68000 z68 version 1 replay test
; Written by Franck 'hitchhikr' Charlet.
; =======================================================
                        opt     o+
                        opt     all+
                        
; =======================================================
MFP_GPIP                equ     $E88001
MFP_IERA                equ     $E88007
MFP_IERB                equ     $E88009
FM_CTRL                 equ     $E90001
FM_DATA                 equ     $E90003
FM_STATUS               equ     $E90003

_EXIT                   equ     $FF00
_SUPER                  equ     $FF20

_ADPCMOUT               equ     $60
_ADPCMSNS               equ     $66
_ADPCMMOD               equ     $67
_OPMINTST               equ     $6A
TRAP_IOSCALL            equ     15

; 1 to show the time taken by the replay
Z68_RASTER_TIME         equ     1

; =======================================================
DOS                     MACRO
                        dc.w    \1
                        ENDM

IOCS                    MACRO
                        IFNE    \1<$80
                            moveq   #\1,d0
                        ELSE
                            moveq   #-($100-\1),d0
                        ENDC
                        trap    #TRAP_IOSCALL
                        ENDM

; =======================================================
start:
                        clr.l   -(a7)
                        DOS     _SUPER
                        lea     z68_vars(pc),a6
                        move.b  MFP_IERB,old_MFP_IERB-z68_vars(a6)                       
                        move.b  #%00000000,MFP_IERA 
                        move.b  #%00000000,MFP_IERB

                        lea     z68_module-z68_vars(a6),a0
                        bsr     z68_start
                        beq     .error_init

; =======================================================
.loop:
                        bsr     wait_vblank
                        tst.b   z68_playing_flag-z68_vars(a6)
                        bne     .loop

; =======================================================
.error_init:
                        bsr     z68_stop
                        move.b  old_MFP_IERB-z68_vars(a6),MFP_IERB
                        DOS     _EXIT

; =======================================================
wait_vblank:
                        lea     MFP_GPIP,a0
wait_vblank_1:
                        btst.b  #4,(a0)
                        beq     wait_vblank_1
wait_vblank_2:
                        btst.b  #4,(a0)
                        bne     wait_vblank_2
                        rts

old_MFP_IERB:           dc.b    0
                        even

; =======================================================
; X68000 z68 version 1 replay
; Written by Franck 'hitchhikr' Charlet.
; =======================================================
Z68_VERSION             equ     1
z68_stop:
                        movem.l d0-d3/a1/a2/a6,-(a7)
                        lea     z68_vars(pc),a6
                        lea     FM_DATA,a2
                        sf.b    z68_playing_flag-z68_vars(a6)
                        sub.l   a1,a1
                        IOCS    _OPMINTST
                        moveq   #32-1,d3
                        moveq   #$60,d2
.clear_vols:
                        move.b  d2,d0
                        moveq   #127,d1
                        bsr     z68_set_ym_reg
                        addq.b  #1,d2
                        dbra    d3,.clear_vols
                        movem.l (a7)+,d0-d3/a1/a2/a6
                        rts
z68_start:
                        movem.l d1/a0/a1/a2/a6,-(a7)
                        lea     z68_vars(pc),a6
                        lea     FM_DATA,a2
                        bsr     z68_stop
                        st.b    z68_playing_flag-z68_vars(a6)
                        ; install the OPM interrupt
                        moveq   #_OPMINTST,d0
                        lea     z68_interrupt-z68_vars(a6),a1
                        trap    #15
                        moveq   #0,d0
                        cmp.l   #'z68'<<8|Z68_VERSION,(a0)+
                        bne     .format_error
                        move.l  (a0)+,z68_loop-z68_vars(a6)
                        move.l  (a0)+,d0
                        add.l   a0,d0
                        ; -2 to pass over the numb er of samples
                        sub.l   #12-2,d0
                        move.l  d0,z68_pcm-z68_vars(a6)
                        ; center
                        move.b  #3,z68_panning-z68_vars(a6)
                        move.w  #4<<8,z68_adpcm_rate-z68_vars(a6)
                        move.w  (a0)+,z68_music_speed-z68_vars(a6)
                        move.w  z68_music_speed-z68_vars(a6),z68_music_speed_count-z68_vars(a6)
                        moveq   #%1000000,d1
                        moveq   #$1b,d0
                        bsr     z68_set_ym_reg
                        ; initial tempo (60 hz)
                        move.b  #191,d1
                        moveq   #$12,d0
                        bsr     z68_set_ym_reg
                        bsr     z68_enable_timer
                        move.l  a0,z68_music_start-z68_vars(a6)
                        move.l  a0,z68_music_ptr-z68_vars(a6)
                        moveq   #1,d0
.format_error:
                        movem.l (a7)+,d1/a0/a1/a2/a6
                        tst.l   d0
                        rts
z68_enable_timer:
                        ; set/reset the timer
                        moveq   #$14,d0
                        moveq   #%00101010,d1
                        ; no rts
z68_set_ym_reg:
                        tst.b   FM_STATUS-FM_DATA(a2)
                        bmi     z68_set_ym_reg
                        ; reg selection
                        move.b  d0,FM_CTRL-FM_DATA(a2)
.wait_fm_chip:
                        tst.b   FM_STATUS-FM_DATA(a2)
                        bmi     .wait_fm_chip
                        ; write datum
                        move.b  d1,FM_DATA-FM_DATA(a2)
                        rts
z68_interrupt:
                        movem.l d0-d3/a0-a2/a6,-(a7)
                        lea     z68_vars(pc),a6
                        lea     FM_DATA,a2
                    IFNE Z68_RASTER_TIME
                        move.w  #$ffe,$E82200
                    ENDC
                        tst.b   z68_playing_flag-z68_vars(a6)
                        beq     .no_play
                        move.l  z68_music_ptr-z68_vars(a6),a0
                        addq.w  #1,z68_music_speed_count-z68_vars(a6)
                        move.w  z68_music_speed_count-z68_vars(a6),d0
                        cmp.w   z68_music_speed-z68_vars(a6),d0
                        blt     .no_new_row
                        moveq   #0,d3
                        ; new row
                        clr.w   z68_music_speed_count-z68_vars(a6)
                        bra     .next_cmd
.no_new_row:
                        ; we drift with YM commands
                        ; until with reach the next new row event
                        moveq   #1,d3
.next_cmd:
                        moveq   #0,d0
                        moveq   #0,d2
                        move.b  (a0)+,d2
                        cmp.b   #$40,d2
                        beq     .ext_cmd
                        cmp.b   #$80,d2
                        beq     .end_song
                        ; ym commands in new row
                        tst.b   d3
                        beq     .new_row_event
                        cmp.b   #$83,d2
                        bne     .empty_row_drift
                        subq.l  #1,a0
                        bra     .next_tick
.empty_row_drift:
                        ; wait for the next new row event
                        cmp.b   #$81,d2
                        bne     .new_row_event
                        subq.l  #1,a0
                        bra     .next_tick
.new_row_event:
                        ; skip new row command
                        cmp.b   #$81,d2
                        bne     .skip
                        ; skip it
                        addq.l  #1,a0
.skip:
                        cmp.b   #$83,d2
                        bne     .empty_row
                        bra     .next_tick
.empty_row:
                        move.b  -(a0),d2
                        addq.l  #1,a0
                        ; OKI data
                        cmp.w   #$40,d2
                        bgt     .fill_ym
                        tst.b   d2
                        bne     .adpcm_off
                        moveq   #0,d1
                        IOCS    _ADPCMMOD
                        bra     .done_data
.adpcm_off:
                        cmp.b   #2,d2
                        bne     .adpcm_panning
                        move.b  (a0)+,z68_panning-z68_vars(a6)
                        bra     .done_data
.adpcm_panning:
                        cmp.b   #2,d2
                        bne     .adpcm_clock
                        move.b  (a0)+,d0
                        ; 
                        bra     .done_data
.adpcm_clock:
                        cmp.b   #2,d2
                        bne     .adpcm_rate
                        move.b  (a0)+,d0
                        lsl.w   #3,d0
                        move.w  d0,z68_adpcm_rate-z68_vars(a6)
                        bra     .done_data
.adpcm_rate:
                        cmp.b   #1,d2
                        bne     .adpcm_sample
                        move.b  (a0)+,d0
                        move.l  d2,-(a7)
                        move.l  z68_pcm-z68_vars(a6),a1
                        lsl.l   #3,d0
                        ; Number of bytes of playback data
                        move.l  4(a1,d0.l),d2
                        ; wav address
                        add.l   (a1,d0.l),a1
                        moveq   #0,d1
                        ; bit 15 Wait mode (0: Normal 1: No wait)
                        bset    #15,d1
                        ; bit 1~0 Output mode (0: None 1: Left 2: Right 3: Left and right)
                        move.b  z68_panning-z68_vars(a6),d1
                        ; bit 10~8 Sampling frequency (0:3.9kHz 1:5.2kHz 2:7.8kHz 3:10.4kHz 4:15.6kHz)
                        or.w    z68_adpcm_rate-z68_vars(a6),d1
                        IOCS    _ADPCMOUT
                        move.l  (a7)+,d2
.adpcm_sample:
                        bra     .done_data
.fill_ym:
                        ; YM data
                        sub.b   #$40,d2
                        ; fill until we reach an end of row event
                        subq.b  #1,d2
.send_loop:
                        tst.b   FM_STATUS-FM_DATA(a2)
                        bmi     .send_loop
                        ; reg selection
                        move.b  (a0)+,FM_CTRL-FM_DATA(a2)
.wait_fm_chip:
                        tst.b   FM_STATUS-FM_DATA(a2)
                        bmi     .wait_fm_chip
                        ; write datum
                        move.b  (a0)+,FM_DATA-FM_DATA(a2)
                        dbf     d2,.send_loop
.done_data:
                        ; peek
                        move.b  (a0),d2
                        ; continue to drift
                        tst.b   d3
                        bne     .next_tick
                        ; more YM events ?
                        cmp.b   #$82,d2
                        bne     .next_cmd 
                        ; pass it
                        addq.l  #1,a0
                        bra     .next_tick
.end_song:
                        move.l  z68_loop-z68_vars(a6),a0
                        add.l   z68_music_start-z68_vars(a6),a0
                        move.l  a0,z68_music_ptr-z68_vars(a6)
                        ; no loop
                        tst.l   z68_music_start-z68_vars(a6)
                        bne     .next_cmd
                        ; end song play
                        sf.b    z68_playing_flag-z68_vars(a6)
                        bra     .no_play
.ext_cmd:
                        move.b  (a0)+,d0
                        beq     .new_speed
                        ; new tempo
                        move.b  (a0)+,d1
                        moveq   #$12,d0
                        bsr     z68_set_ym_reg
                        bra     .next_cmd
.new_speed:
                        move.b  (a0)+,d0
                        move.w  d0,z68_music_speed-z68_vars(a6)
                        bra     .next_cmd
.next_tick:
                        move.l  a0,z68_music_ptr-z68_vars(a6)
.no_play:
                        bsr     z68_enable_timer
                    IFNE Z68_RASTER_TIME
                        move.w  #0,$E82200
                    ENDC
                        movem.l (a7)+,d0-d3/a0-a2/a6
                        rte

; =======================================================
z68_vars:
z68_music_speed_count:  dc.w    0
z68_music_speed:        dc.w    0
z68_music_start:        dc.l    0
z68_music_ptr:          dc.l    0
z68_loop:               dc.l    0
z68_pcm:                dc.l    0
z68_adpcm_rate:         dc.w    0
z68_panning:            dc.b    0
z68_playing_flag:       dc.b    0
                        even

; =======================================================
z68_module:             incbin  "boomer.z68"
                        end

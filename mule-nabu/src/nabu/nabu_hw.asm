; nabu_hw.asm -- the NABU's keyboard, VDP reads and HCCA receive loop.
; C calls answer in L (H = 0), as src/msx/msxbios.asm's.

        SECTION code_user

; ---- the keyboard -----------------------------------------------------------
; Our own keyboard interrupt handler instead of z88dk's (which keeps ONE key and
; parses joystick messages itself): every code the keyboard sends (keys, key-ups,
; joystick prefixes 80h-83h and joystick bytes A0h-BFh) goes into a 16-code queue
; that input.c reads. The keyboard's 8251 holds only about two codes (~2.8 ms):
; while interrupts are off (an HCCA transaction) codes are taken by polling it
; (kb_poll, from hcca_rx_n's wait loops and nstrans.c's drain), so none is lost.
        PUBLIC  _kb_install, _kb_get, _kb_poll

_kb_install:                            ; the IM 2 table's keyboard entry (z88dk's: I * 256 + 4)
        di
        ld      a,i
        ld      h,a
        ld      l,4
        ld      de,kb_isr
        ld      (hl),e
        inc     hl
        ld      (hl),d
        ei
        ret

kb_isr:
        push    af
        push    de
        push    hl
        call    kb_take
        pop     hl
        pop     de
        pop     af
        ei
        ret

; void kb_poll(void): with interrupts off, take a code if one waits (keeps BC, DE, HL)
_kb_poll:
        in      a,($91)                 ; the keyboard 8251's status: bit 1 = a code waits
        and     2
        ret     z
        push    de
        push    hl
        call    kb_take
        pop     hl
        pop     de
        ret

; read the 8251's code into the queue (dropped if full); changes AF, DE, HL
kb_take:
        in      a,($90)
        ld      d,a
        ld      a,(_kbq_w)
        ld      e,a
        inc     a
        and     15
        ld      hl,_kbq_r
        cp      (hl)
        ret     z                       ; full
        ld      (_kbq_w),a
        ld      hl,_kbq
        ld      a,e
        add     a,l
        ld      l,a
        adc     a,h
        sub     l
        ld      h,a
        ld      (hl),d
        ret

; uint8_t kb_get(void): the next code, 0 if none (the handler only writes kbq_w)
_kb_get:
        ld      a,(_kbq_r)
        ld      hl,_kbq_w
        cp      (hl)
        jr      z,kbg_none
        ld      e,a
        inc     a
        and     15
        ld      (_kbq_r),a
        ld      hl,_kbq
        ld      d,0
        add     hl,de
        ld      l,(hl)
        ld      h,0
        ret
kbg_none:
        ld      hl,0
        ret

; z88dk's console (fputc_cons) is linked to this instead (build-nabu.bat: -pragma-redirect), so
; its driver and font stay out: the game draws its own text.
        PUBLIC  _fputc_cons_stub
_fputc_cons_stub:
        ret

; ---- the VDP ------------------------------------------------------------------
; The frame interrupt (plat.c's clock and the music): our handler in the IM 2 table's
; VDP entry (z88dk's: I * 256 + 6). While a VRAM burst runs (vdp_busy: vdp.h's
; VDP_LOCK) it leaves the VDP alone, as reading the status resets its address latch:
; it takes the VDP out of the AY's interrupt mask and sets vdp_pend; VDP_UNLOCK calls
; vdp_catch_up, which puts it back, and the interrupt (still raised) comes at once.
        PUBLIC  _vdp_install, _vdp_catch_up, _vdp_busy, _vdp_pend
        EXTERN  _vdp_frame, _int_mask

_vdp_install:
        di
        ld      a,i
        ld      h,a
        ld      l,6
        ld      de,vdp_isr
        ld      (hl),e
        inc     hl
        ld      (hl),d
        ei
        ret

vdp_isr:
        push    af
        ld      a,(_vdp_busy)
        or      a
        jr      nz,vi_defer
        push    bc
        push    de
        push    hl
        push    ix
        push    iy
        call    _vdp_frame
        pop     iy
        pop     ix
        pop     hl
        pop     de
        pop     bc
        pop     af
        ei
        ret
vi_defer:
        ld      a,1
        ld      (_vdp_pend),a
        ld      a,14                    ; (every other AY access runs with interrupts off)
        out     ($41),a
        ld      a,(_int_mask)
        and     $EF                     ; the VDP out of the mask
        out     ($40),a
        pop     af
        ei
        ret

_vdp_catch_up:
        di
        xor     a
        ld      (_vdp_pend),a
        ld      a,14
        out     ($41),a
        ld      a,(_int_mask)
        out     ($40),a
        ei
        ret

; uint8_t vdp_status(void): the status register (frame flag, fifth sprite). In
; assembly because z88dk's -SO3 peephole rules dropped the IN from plat.c's
; frame_tick (it read vdp_seen instead: the frame flag was never seen).
        PUBLIC  _vdp_status
_vdp_status:
        in      a,($A1)
        ld      l,a
        ld      h,0
        ret

; void vram_read8(void): 8 VRAM bytes from vrd_addr into vrd_dst, each with its
; own read address: the PICO9918 v1.2 (in many NABUs) slips a byte on
; sequential reads; addressed reads are always right. The EX (SP),HL pair gives
; a real TMS9918A the time it needs to fetch the byte after a read address.
        PUBLIC  _vram_read8, _vrd_addr, _vrd_dst
_vram_read8:
        ld      a,1
        ld      (_vdp_busy),a
        ld      hl,(_vrd_dst)
        ld      de,(_vrd_addr)
        ld      b,8
vr8_loop:
        ld      a,e
        out     ($A1),a
        ld      a,d
        and     $3F
        out     ($A1),a
        ex      (sp),hl
        ex      (sp),hl
        in      a,($A0)
        ld      (hl),a
        inc     hl
        inc     de
        djnz    vr8_loop
        xor     a
        ld      (_vdp_busy),a
        ld      a,(_vdp_pend)
        or      a
        ret     z
        jp      _vdp_catch_up

; uint8_t hcca_rx_n(void): read an HCCA reply into hcca_dst, in ONE call: a
; byte comes every ~320 T-states and C glue between two reads can take as long
; (a byte not read before the next lands is lost). The receive mask (80h) is set
; once and port B read in a tight loop, as NABU-LIB's "Di" routines do.
;   hcca_pre = 0: hcca_cnt bytes.
;   hcca_pre = 1 or 4: that many prefix bytes first (into hcca_hdr; hcca_npre
;   says how many came), then a payload whose length is the prefix's first
;   byte (pre 4: only if the other three are 0, else none), at most hcca_cnt.
; hcca_tmo1 polls (~97 T-states each, with the keyboard and hush looks) wait for the first byte, hcca_tmoN for
; each next. Returns the payload bytes read in A and L (H = 0).
; A reply that keeps us waiting (interrupts are off, so the music can't play on): after
; ~45 ms the AY's three volumes go to 0, so no note hangs; sound.c's snd_frame puts the
; music back on the next frame (snd_hushed).
        PUBLIC  _hcca_rx_n, _snd_hush
        EXTERN  _hcca_dst, _hcca_cnt, _hcca_tmo1, _hcca_tmoN, _hcca_left
        EXTERN  _hcca_pre, _hcca_npre, _hcca_want, _hcca_hdr, _snd_hushed

rx_slow:                                ; every 256 polls without a byte (keeps BC, DE, HL)
        push    hl
        ld      hl,rx_slowc
        inc     (hl)
        ld      a,(hl)
        cp      8                       ; 8 x 256 polls (~97 T-states each): ~45 ms
        call    z,_snd_hush
        ld      a,15                    ; back to port B, which the loop polls
        out     ($41),a
        pop     hl
        ret

; void snd_hush(void): the AY's three volumes to 0 (once), snd_hushed set: sound.c's
; snd_frame puts the music back. Also from C (nstrans.c's drain, plat_init). Changes
; A, L; leaves the latch at 14; call it with interrupts off or before they matter.
_snd_hush:
        ld      a,(_snd_hushed)
        or      a
        ret     nz
        inc     a
        ld      (_snd_hushed),a
        ld      l,8
rs_vol:
        ld      a,l
        out     ($41),a
        xor     a
        out     ($40),a
        inc     l
        ld      a,l
        cp      11
        jr      nz,rs_vol
        ld      a,14
        out     ($41),a
        ret

_hcca_rx_n:
        xor     a
        ld      (rx_slowc),a
        ld      a,14
        out     ($41),a
        ld      a,$80
        out     ($40),a
        ld      a,15
        out     ($41),a                 ; latch left at port B: we poll it
        xor     a
        ld      (_hcca_npre),a
        ld      a,(_hcca_pre)
        or      a
        jr      z,rxn_plain
        ; ---- the prefix
        ld      d,a
        ld      hl,_hcca_hdr
        ld      bc,(_hcca_tmo1)
        ld      e,1
rxn_p1poll:
        in      a,($40)
        and     1
        jr      nz,rxn_p1got
        in      a,($91)                 ; a keyboard code waiting? (interrupts are off)
        and     2
        call    nz,_kb_poll
        dec     bc
        ld      a,c
        or      a
        call    z,rx_slow
        ld      a,b
        or      c
        jr      nz,rxn_p1poll
        jp      rxn_none                ; timed out: npre says how far (jp: out of jr's reach)
rxn_p1got:
        dec     e
        jr      nz,rxn_p1ns
        ld      (_hcca_left),bc         ; polls left: how long the reply took
rxn_p1ns:
        in      a,($80)
        ld      (hl),a
        inc     hl
        ld      a,(_hcca_npre)
        inc     a
        ld      (_hcca_npre),a
        dec     d
        jr      z,rxn_p1end
        ld      bc,(_hcca_tmoN)
        jr      rxn_p1poll
rxn_p1end:
        ld      a,(_hcca_pre)
        cp      1
        jr      z,rxn_cnt
        ld      a,(_hcca_hdr+1)         ; an int32: 0 .. 255 only
        ld      hl,_hcca_hdr+2
        or      (hl)
        inc     hl
        or      (hl)
        jr      nz,rxn_none
rxn_cnt:
        ld      a,(_hcca_hdr)
        ld      b,a
        ld      a,(_hcca_cnt)
        cp      b
        jr      c,rxn_have              ; room for fewer: that many
        ld      a,b
rxn_have:
        ld      (_hcca_want),a
        ld      d,a
        or      a
        jr      z,rxn_none
        ld      bc,(_hcca_tmoN)
        ld      e,0                     ; (the left-over is not stored again)
        jr      rxn_start
        ; ---- no prefix
rxn_plain:
        ld      a,(_hcca_cnt)
        ld      (_hcca_want),a
        ld      d,a
        or      a
        jr      z,rxn_none
        ld      bc,(_hcca_tmo1)
        ld      e,1
rxn_start:
        ld      hl,(_hcca_dst)
rxn_poll:
        in      a,($40)
        and     1
        jr      nz,rxn_got
        in      a,($91)                 ; a keyboard code waiting? (interrupts are off)
        and     2
        call    nz,_kb_poll
        dec     bc
        ld      a,c
        or      a
        call    z,rx_slow
        ld      a,b
        or      c
        jr      nz,rxn_poll
        jr      rxn_done                ; timed out
rxn_got:
        dec     e
        jr      nz,rxn_nostore
        ld      (_hcca_left),bc
rxn_nostore:
        in      a,($80)
        ld      (hl),a
        inc     hl
        dec     d
        jr      z,rxn_done
        ld      bc,(_hcca_tmoN)
        jr      rxn_poll
rxn_done:
        ld      a,(_hcca_want)
        sub     d                       ; got = wanted - still to come
        ld      l,a
        ld      h,0
        ret
rxn_none:
        xor     a
        ld      l,a
        ld      h,a
        ret

        SECTION bss_user
_kbq:           defs    16
_kbq_r:         defs    1
_kbq_w:         defs    1
_vrd_addr:      defs    2
_vrd_dst:       defs    2
_vdp_busy:      defs    1
_vdp_pend:      defs    1
rx_slowc:       defs    1

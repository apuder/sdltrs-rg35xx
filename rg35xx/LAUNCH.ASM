; LAUNCH - LDOS 5.3.1 Model I / III game menu, 64 columns x 16 rows.
; Assemble on the host: pasmo --bin LAUNCH.ASM LAUNCH.BIN LAUNCH.SYM
; build.py wraps LAUNCH.BIN in TRS-80 /CMD load and transfer records.
;
; Reads the generated LIST/TXT:4 (two header lines, then title + spaces
; + NAME/CMD or NAME/BAS + TAB + control mapping).
; CR, LF and CRLF are accepted. Titles are clipped
; to 47 display columns; filenames are kept intact. Limit: 250 games,
; 127 bytes before TAB per input line; selected mapping up to 4095 bytes.
; Records live at 7000h..AE7Fh, below XTRSHARD.
; Selected mapping lives at B000h..BFFFh; all other mappings are skipped.
; Files are closed before the keyboard loop and before handing off to DOS.
; LDOS calls: @FSPEC 441Ch, @OPEN 4424h, @GET 0013h, @CLOSE 4428h,
; @KBD 002Bh, @DSPLY 4467h, @EXIT 402Dh; Model III @CMNDI is 4299h,
; Model I @CMNDI is 4405h. No emulator-specific instructions are used.

        org 5200h
ROWS    equ 13                  ; Lines 3..15, less any scroll indicators.
MAXGAME equ 250
TABLE   equ 7000h
MAPPING equ 0b000h
MAPEND  equ 0bfffh              ; Last byte reserved for terminating zero.
MAPPORT equ 41                 ; Decimal 41 (29h), full I/O address 0029h.
GUIDEROWS equ 11

start:
        ld (saved_sp),sp
        xor a
        ld (count),a
        ld (selected),a
        ld (top),a
        ld (line_len),a
        ld (skip_mapping),a
        ld (parsed_line),a
        ld (find_mode),a
        ld a,2
        ld (headers),a
        ld hl,list_name
        ld de,fcb
        call 441ch
        jp nz,open_error
        ld hl,sector_buffer
        ld de,fcb
        ld b,0
        call 4424h
        jp nz,open_error
read_loop:
        ld de,fcb
        call 0013h
        jp nz,read_end
        cp 10
        jr z,end_line            ; CRLF produces an ignored empty line.
        cp 13
        jr z,end_line
        ld c,a
        ld a,(skip_mapping)
        or a
        jr nz,mapping_char
        ld a,c
        cp 9                    ; TAB starts the optional control mapping.
        jr nz,buffer_char
        call parse_line         ; Identify the game before its mapping bytes.
        ld a,1
        ld (parsed_line),a
        ld a,1
        ld (skip_mapping),a
        jr read_loop
mapping_char:
        ld a,(find_mode)
        or a
        jr z,read_loop
        ld a,(found_game)
        or a
        jr z,read_loop
        ld a,c
        cp 32
        jp c,mapping_error
        cp 127
        jp nc,mapping_error
        ld hl,(map_write)
        ld de,MAPEND
        or a
        sbc hl,de
        jp nc,mapping_error
        ld hl,(map_write)
        ld (hl),c
        inc hl
        ld (map_write),hl
        jp read_loop
buffer_char:
        ld a,(line_len)
        cp 127
        jp z,line_error
        ld e,a
        ld d,0
        ld hl,line_buffer
        add hl,de
        ld (hl),c
        inc a
        ld (line_len),a
        jp read_loop
end_line:
        ld a,(parsed_line)
        or a
        call z,parse_line
        ld a,(find_mode)
        or a
        jr z,next_line
        ld a,(found_game)
        or a
        jr nz,read_close
next_line:
        xor a
        ld (line_len),a
        ld (skip_mapping),a
        ld (parsed_line),a
        jp read_loop
read_end:
        cp 28                   ; LDOS EOF.
        jp nz,read_error
        ld a,(parsed_line)
        or a
        call z,parse_line       ; Accept final line without CR.
read_close:
        ld de,fcb
        call 4428h
        jp nz,open_error
        ld a,(find_mode)
        or a
        jp nz,mapping_ready
        ld a,(count)
        or a
        jp z,empty_error
        jp redraw

; Parse a complete line. Blank lines and unsupported file extensions are
; ignored. The reader handles the optional TAB and mapping. Search from
; the right so spaces inside a title are retained. Old two-field lists work.
parse_line:
        ld a,(line_len)
        or a
        ret z
        ld b,a
        ld e,a
        ld d,0
        ld hl,line_buffer
        add hl,de
        ld (hl),0
        ld a,(headers)
        or a
        jr z,parse_game
        dec a
        ld (headers),a
        ret
parse_game:
        dec hl
trim_right:
        ld a,(hl)
        cp ' '
        jr nz,find_separator
        ld (hl),0
        dec hl
        djnz trim_right
        ret
find_separator:
        ld a,(hl)
        cp ' '
        jr z,separator
        dec hl
        djnz find_separator
        ret
separator:
        ld (hl),0
        inc hl
        ld (filename),hl
        dec hl
trim_title:
        dec hl
        push hl
        ld de,line_buffer
        or a
        sbc hl,de
        pop hl
        ret c
        ld a,(hl)
        cp ' '
        jr nz,check_file
        ld (hl),0
        jr trim_title
check_file:
        ld hl,(filename)
        ld b,0
file_length:
        ld a,(hl)
        or a
        jr z,file_end
        ; Only uppercase alphanumerics and slash in a generated filespec.
        cp '/'
        jr z,file_char_ok
        cp '0'
        ret c
        cp '9'+1
        jr c,file_char_ok
        cp 'A'
        ret c
        cp 'Z'+1
        ret nc
file_char_ok:
        inc hl
        inc b
        ld a,b
        cp 13
        ret nc
        jr file_length
file_end:
        ld a,b
        cp 5
        ret c
        dec hl
        dec hl
        dec hl
        dec hl
        ld a,(hl)
        cp '/'
        ret nz
        inc hl
        ld a,(hl)
        cp 'B'
        jr z,check_bas
        cp 'C'
        ret nz
        inc hl
        ld a,(hl)
        cp 'M'
        ret nz
        inc hl
        ld a,(hl)
        cp 'D'
        ret nz
        xor a
        jr keep_game
check_bas:
        inc hl
        ld a,(hl)
        cp 'A'
        ret nz
        inc hl
        ld a,(hl)
        cp 'S'
        ret nz
        ld a,1
keep_game:
        ld (kind),a
        ld a,(find_mode)
        or a
        jp nz,find_game
        ld a,(count)
        cp MAXGAME
        jp z,capacity_error
        call record_address
        ld (record),hl
        ld d,h
        ld e,l
        inc de
        ld (hl),0
        ld bc,63
        ldir
        ld de,(record)
        ld hl,line_buffer
        ld b,47
copy_title:
        ld a,(hl)
        or a
        jr z,copy_filename
        ld (de),a
        inc hl
        inc de
        djnz copy_title
copy_filename:
        ld hl,(record)
        ld de,48
        add hl,de
        ex de,hl
        ld hl,(filename)
copy_file_loop:
        ld a,(hl)
        or a
        jr z,append_drive
        ld (de),a
        inc hl
        inc de
        jr copy_file_loop
append_drive:
        ld a,':'
        ld (de),a
        inc de
        ld a,'4'
        ld (de),a
        ld hl,(record)
        ld de,63
        add hl,de
        ld a,(kind)
        ld (hl),a
        ld hl,count
        inc (hl)
        ret

; Second pass counts exactly the valid records accepted by the first pass.
find_game:
        ld a,(selected)
        ld hl,scan_game
        cp (hl)
        jr z,game_found
        inc (hl)
        ret
game_found:
        ld a,1
        ld (found_game),a
        ret

; A -> address of a 64-byte record. Destroys DE.
record_address:
        ld l,a
        ld h,0
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        add hl,hl
        ld de,TABLE
        add hl,de
        ret

; Reuse each indicator row for a game when that indicator is unnecessary.
window_layout:
        ld b,ROWS
        ld hl,3c80h
        ld a,(top)
        or a
        jr z,window_start
        dec b
        ld hl,3cc0h
window_start:
        ld (screen_row),hl
        ld c,a
        ld a,(count)
        sub c
        cp b
        ld a,0
        jr c,window_ready
        jr z,window_ready
        dec b
        inc a
window_ready:
        ld (has_below),a
        ld a,b
        ld (visible_rows),a
        ret

redraw:
        call window_layout
        ld a,(top)
        ld b,a
        ld a,(visible_rows)
        add a,b
        ld b,a
        ld a,(selected)
        cp b
        jr c,window_fits
        ld hl,top
        inc (hl)
        jr redraw              ; An appearing indicator can consume a row.
window_fits:
        ld hl,3c00h
        ld de,3c01h
        ld bc,1023
        ld (hl),' '
        ldir
        ld hl,banner
        ld de,3c00h
        call screen_text
        ld hl,help_text
        ld de,3fc0h
        call screen_text
        ld hl,3c40h             ; Line 2: full-width graphics divider.
        ld de,3c41h
        ld bc,63
        ld (hl),140
        ldir
        ld a,(top)
        or a
        jr z,no_above
        ld hl,above_text
        ld de,3c80h             ; Line 3: games above the window.
        call screen_text
no_above:
        ld a,(has_below)
        or a
        jr z,no_below
        ld hl,below_text
        ld de,3f80h             ; Line 15: games below the window.
        call screen_text
no_below:
        ld a,(top)
        ld (drawing),a
        ld a,(visible_rows)
        ld b,a
paint_row:
        push bc
        ld a,(drawing)
        ld c,a
        ld a,(count)
        cp c
        jr z,paint_done
        ld a,(selected)
        cp c
        ld de,(screen_row)
        jr nz,no_cursor
        ld a,'-'
        ld (de),a
        inc de
        ld a,'>'
        ld (de),a
        dec de
no_cursor:
        inc de
        inc de
        inc de
        ld a,c
        push de
        call record_address
        pop de
        call screen_text
        ld hl,screen_row
        ld a,(hl)
        add a,64
        ld (hl),a
        jr nc,row_address_ok
        inc hl
        inc (hl)
row_address_ok:
        ld hl,drawing
        inc (hl)
        pop bc
        djnz paint_row
        jr keyboard
paint_done:
        pop bc
keyboard:
        call 002bh
        or a
        jr z,keyboard
        cp 1                    ; BREAK from LDOS keyboard driver.
        jp z,quit
        cp 3
        jp z,quit
        cp 27
        jp z,quit
        cp 13
        jp z,show_controls
        cp 10                   ; Normal down arrow.
        jr z,down
        cp 88h                  ; LDOS extended cursor mode down arrow.
        jr z,down
        cp 5bh                  ; Normal up arrow (Model I / III).
        jr z,up
        cp 82h                  ; LDOS extended cursor mode up arrow.
        jr z,up
        cp 11                   ; Also accept control-K style up.
        jr z,up
        jp keyboard
up:
        ld a,(selected)
        or a
        jp z,keyboard
        dec a
        ld (selected),a
        ld b,a
        ld a,(top)
        cp b
        jp c,redraw
        jp z,redraw
        ld a,b
        ld (top),a
        jp redraw
down:
        ld a,(selected)
        inc a
        ld b,a
        ld a,(count)
        cp b
        jp z,keyboard
        ld a,b
        ld (selected),a
        jp redraw

; Zero-terminated string to video RAM. No cursor or scrolling side effects.
screen_text:
        ld a,(hl)
        or a
        ret z
        ld (de),a
        inc hl
        inc de
        jr screen_text

; Reload only the selected mapping, leaving the game table intact. This avoids
; an aggregate catalog-mapping limit and keeps each mapping byte unchanged.
show_controls:
        xor a
        ld (line_len),a
        ld (skip_mapping),a
        ld (parsed_line),a
        ld (scan_game),a
        ld (found_game),a
        inc a
        ld (find_mode),a
        ld a,2
        ld (headers),a
        ld hl,MAPPING
        ld (map_write),hl
        ld (hl),0
        ld hl,list_name
        ld de,fcb
        call 441ch
        jp nz,open_error
        ld hl,sector_buffer
        ld de,fcb
        ld b,0
        call 4424h
        jp nz,open_error
        jp read_loop
mapping_ready:
        ld a,(found_game)
        or a
        jp z,missing_game_error
        ld hl,(map_write)
        ld (hl),0
        call validate_mapping
guide_first:
        ld hl,default_guide
        ld (guide_default_next),hl
        ld hl,MAPPING
guide_set_start:
        ld (guide_next),hl
guide_redraw:
        ld hl,(guide_next)
        call wipe_video
        ld a,(selected)
        call record_address
        ld de,3c00h
        call screen_text
        ld hl,guide_heading
        ld de,3c40h
        call screen_text
        ld hl,guide_divider
        ld de,3c80h
        call screen_text
        ld hl,guide_help
        ld de,3fc0h
        call screen_text
        xor a
        ld (guide_rows),a
        ld hl,3cc0h
        ld (guide_screen),hl
guide_draw:
        call next_binding
        jr nc,guide_finished
        ld hl,guide_line
        ld de,(guide_screen)
        call screen_text
        ld hl,(guide_screen)
        ld de,64
        add hl,de
        ld (guide_screen),hl
        ld hl,guide_rows
        inc (hl)
        ld a,(hl)
        cp GUIDEROWS
        jr c,guide_draw
guide_finished:
guide_release:
        ld a,(3840h)            ; Require release of the menu's ENTER key.
        and 1
        jr nz,guide_release
guide_keyboard:
        call 002bh
        or a
        jr z,guide_keyboard
        cp 13
        jp z,send_mapping
        cp 1
        jr z,guide_back
        cp 3
        jr z,guide_back
        cp 27
        jr z,guide_back
        cp ' '
        jr nz,guide_keyboard
        ld hl,(guide_next)
        ld a,(hl)
        or a
        jp z,guide_first
        jp guide_redraw
guide_back:
        xor a
        ld (find_mode),a
        jp redraw

; Validate compact CONTROL:KEY pairs before transmitting any bytes.
; The builder and emulator validate token names; here enforce delimiters,
; nonempty chord members and printable uppercase token characters.
validate_mapping:
        ld hl,MAPPING
        ld a,(hl)
        or a
        ret z                   ; An empty mapping clears the previous map.
        xor a
        ld (in_action),a
        ld (token_used),a
validate_char:
        ld a,(hl)
        or a
        jr z,validate_end
        cp ':'
        jr z,validate_colon
        cp ','
        jr z,validate_comma
        cp '+'
        jr z,validate_plus
        cp '@'
        jr z,validate_token
        cp '_'
        jr z,validate_token
        cp '0'
        jp c,mapping_format_error
        cp '9'+1
        jr c,validate_token
        cp 'A'
        jp c,mapping_format_error
        cp 'Z'+1
        jp nc,mapping_format_error
validate_token:
        ld a,1
        ld (token_used),a
validate_advance:
        inc hl
        jr validate_char
validate_plus:
        ld a,(token_used)
        or a
        jp z,mapping_format_error
        xor a
        ld (token_used),a
        jr validate_advance
validate_colon:
        ld a,(token_used)
        or a
        jp z,mapping_format_error
        ld a,(in_action)
        or a
        jp nz,mapping_format_error
        inc a
        ld (in_action),a
        xor a
        ld (token_used),a
        jr validate_advance
validate_comma:
        call validate_pair_end
        xor a
        ld (in_action),a
        ld (token_used),a
        jr validate_advance
validate_end:
        ; Also rejects a trailing comma, colon or plus.
validate_pair_end:
        ld a,(in_action)
        or a
        jp z,mapping_format_error
        ld a,(token_used)
        or a
        jp z,mapping_format_error
        ret

; Produce table rows: Left DPAD, A, B and X defaults, then custom bindings.
; A direct custom binding replaces its default row. Chords leave the ordinary
; defaults visible; the footer explains their precedence while held.
next_binding:
        ld hl,(guide_default_next)
        ld e,(hl)
        inc hl
        ld d,(hl)
        ld a,d
        or e
        jr z,custom_binding
        inc hl
        ld c,(hl)
        inc hl
        ld b,(hl)
        inc hl
        ld (guide_default_next),hl
        push bc
        ex de,hl
        call has_custom_trigger
        pop hl
        jr c,next_binding       ; The exact control has a custom assignment.
        ld de,guide_line
        ld bc,64
        ldir
        scf
        ret

; HL -> default trigger; carry if an identical single trigger is customized.
; An empty trigger labels an always-visible grouped/reserved default.
has_custom_trigger:
        ld (match_token),hl
        ld a,(hl)
        or a
        ret z
        ld de,MAPPING
match_binding:
        ld hl,(match_token)
match_char:
        ld a,(hl)
        or a
        jr z,match_end
        ld c,a
        ld a,(de)
        cp c
        jr nz,match_skip
        inc de
        inc hl
        jr match_char
match_end:
        ld a,(de)
        cp ':'
        jr z,match_found
match_skip:
        ld a,(de)
        or a
        ret z                   ; No matching custom trigger; carry clear.
        inc de
        cp ','
        jr nz,match_skip
        jr match_binding
match_found:
        scf
        ret

; Format CONTROL:KEY into fixed columns without modifying the payload.
; Clip each column independently and mark long values with an ellipsis.
custom_binding:
        ld hl,(guide_next)
        ld a,(hl)
        or a
        ret z
        push hl
        ld hl,table_empty
        ld de,guide_line
        ld bc,64
        ldir
        pop hl
        ld de,guide_line+2
        ld b,22
custom_control:
        ld a,(hl)
        cp ':'
        jr z,custom_action_start
        ld c,a
        ld a,b
        or a
        jr z,custom_control_clip
        ld a,c
        ld (de),a
        inc de
        dec b
        jr custom_control_advance
custom_control_clip:
        ld a,'.'
        ld (guide_line+21),a
        ld (guide_line+22),a
        ld (guide_line+23),a
custom_control_advance:
        inc hl
        jr custom_control
custom_action_start:
        inc hl
        ld de,guide_line+27
        ld b,34
custom_action:
        ld a,(hl)
        or a
        jr z,custom_end
        cp ','
        jr z,custom_separator
        ld c,a
        ld a,b
        or a
        jr z,custom_action_clip
        ld a,c
        ld (de),a
        inc de
        dec b
        jr custom_action_advance
custom_action_clip:
        ld a,'.'
        ld (guide_line+58),a
        ld (guide_line+59),a
        ld (guide_line+60),a
custom_action_advance:
        inc hl
        jr custom_action
custom_separator:
        inc hl
custom_end:
        ld (guide_next),hl
        scf
        ret

wipe_video:
        ld hl,3c00h
        ld de,3c01h
        ld bc,1023
        ld (hl),' '
        ldir
        ret

; Stream ASCII bytes, including the terminating zero, to full port 0029h.
; BC is fixed so the high port byte does not depend on the transmitted byte.
send_mapping:
        ld hl,MAPPING
        ld bc,MAPPORT
mapping_out:
        ld a,(hl)
        out (c),a
        inc hl
        or a
        jr nz,mapping_out
        jp launch

launch:
        ld a,(selected)
        call record_address
        ld de,63
        add hl,de
        ld a,(hl)
        ld (kind),a
        ld a,(selected)
        call record_address
        ld de,48
        add hl,de
        ld (filename),hl
        ld de,command
        ld a,(kind)
        or a
        jr z,launch_file
        ld hl,basic_prefix
        call command_text
launch_file:
        ld hl,(filename)
        call command_text
        ld a,(kind)
        or a
        jr z,command_end
        ld a,34
        ld (de),a
        inc de
command_end:
        ld a,13
        ld (de),a
        call clear_screen
        ld sp,(saved_sp)
        ld hl,command
        ld a,(0125h)
        cp 'I'
        jp z,4299h              ; Model III LDOS @CMNDI.
        jp 4405h                ; Model I LDOS @CMNDI.
command_text:
        ld a,(hl)
        or a
        ret z
        ld (de),a
        inc hl
        inc de
        jr command_text
quit:
        call clear_screen
        ld sp,(saved_sp)
        jp 402dh
clear_screen:
        ld a,28                 ; Home, then erase to end of display.
        call 0033h
        ld a,31
        call 0033h
        ret

line_error:
        ld hl,long_message
        jr close_error
mapping_error:
        ld hl,mapping_message
        jr close_error
capacity_error:
        ld hl,full_message
        jr close_error
read_error:
        ld hl,read_message
close_error:
        push hl
        ld de,fcb
        call 4428h
        pop hl
        jr show_error
empty_error:
        ld hl,empty_message
        jr show_error
open_error:
        ld hl,open_message
        jr show_error
missing_game_error:
        ld hl,missing_message
        jr show_error
mapping_format_error:
        ld hl,format_message
show_error:
        call 4467h
        ld sp,(saved_sp)
        jp 402dh

list_name:      db 'LIST/TXT:4',13
banner:         db 'TRS-80 GAME LAUNCHER',0
help_text:      db 'DPAD: Choose game     A: Select     B: Exit',0
guide_heading: db '| CONTROL                | TRS-80 KEY                         |',0
guide_divider: db '+------------------------+------------------------------------+',0
guide_help:    db 'A: Launch     B: Back',0
table_empty:   db '|                        |                                    |',0

; Four-byte records: trigger token pointer and complete table-row pointer.
; Grouped direction rows summarize defaults; custom directions are exceptions.
default_guide:
        dw empty_trigger,guide_dpad
        dw trigger_a,guide_a
        dw trigger_b,guide_b
        dw trigger_x,guide_x
        dw 0
empty_trigger: db 0
trigger_a: db 'A',0
trigger_b: db 'B',0
trigger_x: db 'X',0
guide_dpad: db '| Left DPAD              | Arrow keys                         |',0
guide_a: db '| A                      | ENTER                              |',0
guide_b: db '| B                      | BREAK                              |',0
guide_x: db '| X                      | CLEAR                              |',0
above_text:     db '   ^^^',0
below_text:     db '   vvv',0
basic_prefix:   db 'BASIC/CMD:0 RUN"',0
open_message:   db 'Cannot open or close LIST/TXT:4',13,3
read_message:   db 'Error reading LIST/TXT:4',13,3
empty_message:  db 'LIST/TXT:4 contains no CMD or BAS games',13,3
long_message:   db 'LIST/TXT title/filespec exceeds 127 bytes',13,3
full_message:   db 'LIST/TXT exceeds 250 games',13,3
mapping_message: db 'Invalid mapping or mapping exceeds 4095 bytes',13,3
missing_message: db 'Selected game missing from LIST/TXT:4',13,3
format_message: db 'Invalid control mapping (expected CONTROL:KEY pairs)',13,3
saved_sp:       dw 0
filename:       dw 0
record:         dw 0
screen_row:     dw 0
count:          db 0
selected:       db 0
top:            db 0
headers:        db 0
line_len:       db 0
skip_mapping:   db 0
parsed_line:    db 0
find_mode:      db 0
found_game:     db 0
scan_game:      db 0
map_write:      dw 0
guide_next:     dw 0
guide_default_next: dw 0
match_token:    dw 0
guide_screen:   dw 0
guide_rows:     db 0
in_action:     db 0
token_used:    db 0
kind:           db 0
drawing:        db 0
visible_rows:   db 0
has_below:      db 0
command:        ds 64,0
fcb:            ds 32,0
sector_buffer:  ds 256,0
line_buffer:    ds 128,0
guide_line:     ds 64,0
        end start

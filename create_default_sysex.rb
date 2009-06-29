NUM_BOARDS = 1
SYSEX_FILE = "default.syx"

#dev, buzzr, 1
SYSEX_HEADER = [125, 98, 117, 122, 122, 114, 1]
SYSEX_BEGIN = 0xF0
SYSEX_END = 0xF7

SET_BUTTON_DATA = 2

BTN_LED_MIDI_DRIVEN = 0x1
BTN_TOGGLE = 0x2

File.open(SYSEX_FILE, "w"){ |f|
  #encoders
  (NUM_BOARDS * 16).times do |i|
    f.print SYSEX_BEGIN.chr
    SYSEX_HEADER.each do |h|
      f.print h.chr
    end
    f.print SET_BUTTON_DATA.chr
    #index
    f.print i.chr
    #chan
    f.print 0.chr
    #cc num
    f.print i.chr
    #flag
    flags = 0
    f.print flags.chr
    #color
    color = 0
    if i % 2 == 0
      color = ((1 << 2) | (1 << 1)) << 3
      color = color | 1
    else
      color = ((1 << 2)) << 3
      color = color | (1 << 1)
    end
    f.print color.chr
    f.print SYSEX_END.chr
  end
}

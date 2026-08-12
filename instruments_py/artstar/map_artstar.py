# Auto-generated review map for artstar.
# Edit values here, then run the corresponding convert_*.py script.

INSTRUMENTS = {
    'china': {
        "instrument": 'china-X1',
        "articulation": 'edge',
        "parameter": '',
    },
    'crash-litian-16inch': {
        "instrument": 'crash-16',
        "articulation": 'edge',
        "parameter": '',
    },
    'crash-litian-16inch-stop': {
        "instrument": 'crash-16',
        "articulation": 'choke',
        "parameter": '',
    },
    'crash-litian-18inch': {
        "instrument": 'crash-18',
        "articulation": 'edge',
        "parameter": '',
    },
    'crash-litian-18inch-stop': {
        "instrument": 'crash-18',
        "articulation": 'choke',
        "parameter": '',
    },
    'hihat-closed': {
        "instrument": 'hihat-X1',
        "articulation": 'bow[close]',
        "parameter": '-pdl_opn_cls078',
    },
    'hihat-foot': {
        "instrument": 'hihat-X1',
        "articulation": 'pedal',
        "parameter": '',
    },
    'hihat-open': {
        "instrument": 'hihat-X1',
        "articulation": 'edge[open]',
        "parameter": '-pdl_opn_cls043',
    },
    'kick-l': {
        "instrument": 'kick-L1',
        "articulation": 'center',
        "parameter": '',
    },
    'kick-r': {
        "instrument": 'kick-R1',
        "articulation": 'center',
        "parameter": '',
    },
    'ride': {
        "instrument": 'ride-X1',
        "articulation": 'bow',
        "parameter": '',
    },
    'ride-bell': {
        "instrument": 'ride-X1',
        "articulation": 'bell',
        "parameter": '',
    },
    'snare-artstar': {
        "instrument": 'snare-X1',
        "articulation": 'center',
        "parameter": '',
    },
    'snare-ludwig': {
        "instrument": 'snare-X2',
        "articulation": 'center',
        "parameter": '',
    },
    'stagg-12inch': {
        "instrument": 'china-12',
        "articulation": 'edge',
        "parameter": '',
    },
    'stagg-8inch': {
        "instrument": 'china-8',
        "articulation": 'edge',
        "parameter": '',
    },
    'tom1': {
        "instrument": 'tom-X1',
        "articulation": 'center',
        "parameter": '',
    },
    'tom2': {
        "instrument": 'tom-X2',
        "articulation": 'center',
        "parameter": '',
    },
    'tom3': {
        "instrument": 'tom-X3',
        "articulation": 'center',
        "parameter": '',
    },
    'tom4': {
        "instrument": 'tom-X4',
        "articulation": 'center',
        "parameter": '',
    },
    'zilbel': {
        "instrument": 'zilbel-X1',
        "articulation": 'bell',
        "parameter": '',
    },
}

MICS = {
    # Microphone groups for china.
    ('china', 'AMB'): {"keep": True, "code": 'rfs' },			# amb-l-20[srcch0] + amb-r-20[srcch1]
    # ('china', 'HIHAT'): {"keep": False, "code": 'dtm' },			# hihat-20[srcch2]
    # ('china', 'KICK_L'): {"keep": False, "code": 'dim' },			# kick-l-20[srcch3]
    # ('china', 'KICK_R'): {"keep": False, "code": 'dim' },			# kick-r-20[srcch4]
    # ('china', 'KICK_TRIGGER'): {"keep": False, "code": 'tim' },			# kick-trg-20[srcch5]
    ('china', 'OH'): {"keep": True, "code": 'ons' },			# oh-l-20[srcch6] + oh-r-20[srcch7]
    # ('china', 'RIDE'): {"keep": False, "code": 'dtm' },			# ride-20[srcch8]
    # ('china', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# snare-btm-20[srcch9]
    # ('china', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# snare-top-20[srcch10]
    # ('china', 'TOM1'): {"keep": False, "code": 'dtm' },			# tom1-20[srcch11]
    # ('china', 'TOM2'): {"keep": False, "code": 'dtm' },			# tom2-20[srcch12]
    # ('china', 'TOM3'): {"keep": False, "code": 'dtm' },			# tom3-20[srcch13]
    # ('china', 'TOM4'): {"keep": False, "code": 'dtm' },			# tom4-20[srcch14]

    # Microphone groups for crash-litian-16inch.
    ('crash-litian-16inch', 'AMB'): {"keep": True, "code": 'rfs' },			# amb-l-11[srcch0] + amb-r-11[srcch1]
    # ('crash-litian-16inch', 'HIHAT'): {"keep": False, "code": 'dtm' },			# hihat-11[srcch2]
    # ('crash-litian-16inch', 'KICK_L'): {"keep": False, "code": 'dim' },			# kick-l-11[srcch3]
    # ('crash-litian-16inch', 'KICK_R'): {"keep": False, "code": 'dim' },			# kick-r-11[srcch4]
    # ('crash-litian-16inch', 'KICK_TRIGGER'): {"keep": False, "code": 'tim' },			# kick-trg-11[srcch5]
    ('crash-litian-16inch', 'OH'): {"keep": True, "code": 'ons' },			# oh-l-11[srcch6] + oh-r-11[srcch7]
    # ('crash-litian-16inch', 'RIDE'): {"keep": False, "code": 'dtm' },			# ride-11[srcch8]
    # ('crash-litian-16inch', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# snare-btm-11[srcch9]
    # ('crash-litian-16inch', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# snare-top-11[srcch10]
    # ('crash-litian-16inch', 'TOM1'): {"keep": False, "code": 'dtm' },			# tom1-11[srcch11]
    # ('crash-litian-16inch', 'TOM2'): {"keep": False, "code": 'dtm' },			# tom2-11[srcch12]
    # ('crash-litian-16inch', 'TOM3'): {"keep": False, "code": 'dtm' },			# tom3-11[srcch13]
    # ('crash-litian-16inch', 'TOM4'): {"keep": False, "code": 'dtm' },			# tom4-11[srcch14]

    # Microphone groups for crash-litian-16inch-stop.
    ('crash-litian-16inch-stop', 'AMB'): {"keep": True, "code": 'rfs' },			# amb-l-13[srcch0] + amb-r-13[srcch1]
    # ('crash-litian-16inch-stop', 'HIHAT'): {"keep": False, "code": 'dtm' },			# hihat-13[srcch2]
    # ('crash-litian-16inch-stop', 'KICK_L'): {"keep": False, "code": 'dim' },			# kick-l-13[srcch3]
    # ('crash-litian-16inch-stop', 'KICK_R'): {"keep": False, "code": 'dim' },			# kick-r-13[srcch4]
    # ('crash-litian-16inch-stop', 'KICK_TRIGGER'): {"keep": False, "code": 'tim' },			# kick-trg-13[srcch5]
    ('crash-litian-16inch-stop', 'OH'): {"keep": True, "code": 'ons' },			# oh-l-13[srcch6] + oh-r-13[srcch7]
    # ('crash-litian-16inch-stop', 'RIDE'): {"keep": False, "code": 'dtm' },			# ride-13[srcch8]
    # ('crash-litian-16inch-stop', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# snare-btm-13[srcch9]
    # ('crash-litian-16inch-stop', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# snare-top-13[srcch10]
    # ('crash-litian-16inch-stop', 'TOM1'): {"keep": False, "code": 'dtm' },			# tom1-13[srcch11]
    # ('crash-litian-16inch-stop', 'TOM2'): {"keep": False, "code": 'dtm' },			# tom2-13[srcch12]
    # ('crash-litian-16inch-stop', 'TOM3'): {"keep": False, "code": 'dtm' },			# tom3-13[srcch13]
    # ('crash-litian-16inch-stop', 'TOM4'): {"keep": False, "code": 'dtm' },			# tom4-13[srcch14]

    # Microphone groups for crash-litian-18inch.
    ('crash-litian-18inch', 'AMB'): {"keep": True, "code": 'rfs' },			# amb-l-14[srcch0] + amb-r-14[srcch1]
    # ('crash-litian-18inch', 'HIHAT'): {"keep": False, "code": 'dtm' },			# hihat-14[srcch2]
    # ('crash-litian-18inch', 'KICK_L'): {"keep": False, "code": 'dim' },			# kick-l-14[srcch3]
    # ('crash-litian-18inch', 'KICK_R'): {"keep": False, "code": 'dim' },			# kick-r-14[srcch4]
    # ('crash-litian-18inch', 'KICK_TRIGGER'): {"keep": False, "code": 'tim' },			# kick-trg-14[srcch5]
    ('crash-litian-18inch', 'OH'): {"keep": True, "code": 'ons' },			# oh-l-14[srcch6] + oh-r-14[srcch7]
    # ('crash-litian-18inch', 'RIDE'): {"keep": False, "code": 'dtm' },			# ride-14[srcch8]
    # ('crash-litian-18inch', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# snare-btm-14[srcch9]
    # ('crash-litian-18inch', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# snare-top-14[srcch10]
    # ('crash-litian-18inch', 'TOM1'): {"keep": False, "code": 'dtm' },			# tom1-14[srcch11]
    # ('crash-litian-18inch', 'TOM2'): {"keep": False, "code": 'dtm' },			# tom2-14[srcch12]
    # ('crash-litian-18inch', 'TOM3'): {"keep": False, "code": 'dtm' },			# tom3-14[srcch13]
    # ('crash-litian-18inch', 'TOM4'): {"keep": False, "code": 'dtm' },			# tom4-14[srcch14]

    # Microphone groups for crash-litian-18inch-stop.
    ('crash-litian-18inch-stop', 'AMB'): {"keep": True, "code": 'rfs' },			# amb-l-15[srcch0] + amb-r-15[srcch1]
    # ('crash-litian-18inch-stop', 'HIHAT'): {"keep": False, "code": 'dtm' },			# hihat-15[srcch2]
    # ('crash-litian-18inch-stop', 'KICK_L'): {"keep": False, "code": 'dim' },			# kick-l-15[srcch3]
    # ('crash-litian-18inch-stop', 'KICK_R'): {"keep": False, "code": 'dim' },			# kick-r-15[srcch4]
    # ('crash-litian-18inch-stop', 'KICK_TRIGGER'): {"keep": False, "code": 'tim' },			# kick-trg-15[srcch5]
    ('crash-litian-18inch-stop', 'OH'): {"keep": True, "code": 'ons' },			# oh-l-15[srcch6] + oh-r-15[srcch7]
    # ('crash-litian-18inch-stop', 'RIDE'): {"keep": False, "code": 'dtm' },			# ride-15[srcch8]
    # ('crash-litian-18inch-stop', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# snare-btm-15[srcch9]
    # ('crash-litian-18inch-stop', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# snare-top-15[srcch10]
    # ('crash-litian-18inch-stop', 'TOM1'): {"keep": False, "code": 'dtm' },			# tom1-15[srcch11]
    # ('crash-litian-18inch-stop', 'TOM2'): {"keep": False, "code": 'dtm' },			# tom2-15[srcch12]
    # ('crash-litian-18inch-stop', 'TOM3'): {"keep": False, "code": 'dtm' },			# tom3-15[srcch13]
    # ('crash-litian-18inch-stop', 'TOM4'): {"keep": False, "code": 'dtm' },			# tom4-15[srcch14]

    # Microphone groups for hihat-closed.
    ('hihat-closed', 'AMB'): {"keep": True, "code": 'rfs' },			# amb-l-3[srcch0] + amb-r-3[srcch1]
    ('hihat-closed', 'HIHAT'): {"keep": True, "code": 'dtm' },			# hihat-3[srcch2]
    # ('hihat-closed', 'KICK_L'): {"keep": False, "code": 'dim' },			# kick-l-3[srcch3]
    # ('hihat-closed', 'KICK_R'): {"keep": False, "code": 'dim' },			# kick-r-3[srcch4]
    # ('hihat-closed', 'KICK_TRIGGER'): {"keep": False, "code": 'tim' },			# kick-trg-3[srcch5]
    ('hihat-closed', 'OH'): {"keep": True, "code": 'ons' },			# oh-l-3[srcch6] + oh-r-3[srcch7]
    # ('hihat-closed', 'RIDE'): {"keep": False, "code": 'dtm' },			# ride-3[srcch8]
    # ('hihat-closed', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# snare-btm-3[srcch9]
    # ('hihat-closed', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# snare-top-3[srcch10]
    # ('hihat-closed', 'TOM1'): {"keep": False, "code": 'dtm' },			# tom1-3[srcch11]
    # ('hihat-closed', 'TOM2'): {"keep": False, "code": 'dtm' },			# tom2-3[srcch12]
    # ('hihat-closed', 'TOM3'): {"keep": False, "code": 'dtm' },			# tom3-3[srcch13]
    # ('hihat-closed', 'TOM4'): {"keep": False, "code": 'dtm' },			# tom4-3[srcch14]

    # Microphone groups for hihat-foot.
    ('hihat-foot', 'AMB'): {"keep": True, "code": 'rfs' },			# amb-l-4[srcch0] + amb-r-4[srcch1]
    ('hihat-foot', 'HIHAT'): {"keep": True, "code": 'dtm' },			# hihat-4[srcch2]
    # ('hihat-foot', 'KICK_L'): {"keep": False, "code": 'dim' },			# kick-l-4[srcch3]
    # ('hihat-foot', 'KICK_R'): {"keep": False, "code": 'dim' },			# kick-r-4[srcch4]
    # ('hihat-foot', 'KICK_TRIGGER'): {"keep": False, "code": 'tim' },			# kick-trg-4[srcch5]
    ('hihat-foot', 'OH'): {"keep": True, "code": 'ons' },			# oh-l-4[srcch6] + oh-r-4[srcch7]
    # ('hihat-foot', 'RIDE'): {"keep": False, "code": 'dtm' },			# ride-4[srcch8]
    # ('hihat-foot', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# snare-btm-4[srcch9]
    # ('hihat-foot', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# snare-top-4[srcch10]
    # ('hihat-foot', 'TOM1'): {"keep": False, "code": 'dtm' },			# tom1-4[srcch11]
    # ('hihat-foot', 'TOM2'): {"keep": False, "code": 'dtm' },			# tom2-4[srcch12]
    # ('hihat-foot', 'TOM3'): {"keep": False, "code": 'dtm' },			# tom3-4[srcch13]
    # ('hihat-foot', 'TOM4'): {"keep": False, "code": 'dtm' },			# tom4-4[srcch14]

    # Microphone groups for hihat-open.
    ('hihat-open', 'AMB'): {"keep": True, "code": 'rfs' },			# amb-l-7[srcch0] + amb-r-7[srcch1]
    ('hihat-open', 'HIHAT'): {"keep": True, "code": 'dtm' },			# hihat-7[srcch2]
    # ('hihat-open', 'KICK_L'): {"keep": False, "code": 'dim' },			# kick-l-7[srcch3]
    # ('hihat-open', 'KICK_R'): {"keep": False, "code": 'dim' },			# kick-r-7[srcch4]
    # ('hihat-open', 'KICK_TRIGGER'): {"keep": False, "code": 'tim' },			# kick-trg-7[srcch5]
    ('hihat-open', 'OH'): {"keep": True, "code": 'ons' },			# oh-l-7[srcch6] + oh-r-7[srcch7]
    # ('hihat-open', 'RIDE'): {"keep": False, "code": 'dtm' },			# ride-7[srcch8]
    # ('hihat-open', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# snare-btm-7[srcch9]
    # ('hihat-open', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# snare-top-7[srcch10]
    # ('hihat-open', 'TOM1'): {"keep": False, "code": 'dtm' },			# tom1-7[srcch11]
    # ('hihat-open', 'TOM2'): {"keep": False, "code": 'dtm' },			# tom2-7[srcch12]
    # ('hihat-open', 'TOM3'): {"keep": False, "code": 'dtm' },			# tom3-7[srcch13]
    # ('hihat-open', 'TOM4'): {"keep": False, "code": 'dtm' },			# tom4-7[srcch14]

    # Microphone groups for kick-l.
    ('kick-l', 'AMB'): {"keep": True, "code": 'rfs' },			# amb-l-26[srcch0] + amb-r-26[srcch1]
    # ('kick-l', 'HIHAT'): {"keep": False, "code": 'dtm' },			# hihat-26[srcch2]
    ('kick-l', 'KICK_L'): {"keep": True, "code": 'dim' },			# kick-l-26[srcch3]
    ('kick-l', 'KICK_R'): {"keep": False, "code": 'dim' },			# kick-r-26[srcch4]
    # ('kick-l', 'KICK_TRIGGER'): {"keep": False, "code": 'tim' },			# kick-trg-26[srcch5]
    ('kick-l', 'OH'): {"keep": True, "code": 'ons' },			# oh-l-26[srcch6] + oh-r-26[srcch7]
    # ('kick-l', 'RIDE'): {"keep": False, "code": 'dtm' },			# ride-26[srcch8]
    # ('kick-l', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# snare-btm-26[srcch9]
    # ('kick-l', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# snare-top-26[srcch10]
    # ('kick-l', 'TOM1'): {"keep": False, "code": 'dtm' },			# tom1-26[srcch11]
    # ('kick-l', 'TOM2'): {"keep": False, "code": 'dtm' },			# tom2-26[srcch12]
    # ('kick-l', 'TOM3'): {"keep": False, "code": 'dtm' },			# tom3-26[srcch13]
    # ('kick-l', 'TOM4'): {"keep": False, "code": 'dtm' },			# tom4-26[srcch14]

    # Microphone groups for kick-r.
    ('kick-r', 'AMB'): {"keep": True, "code": 'rfs' },			# amb-l-25[srcch0] + amb-r-25[srcch1]
    # ('kick-r', 'HIHAT'): {"keep": False, "code": 'dtm' },			# hihat-25[srcch2]
    ('kick-r', 'KICK_L'): {"keep": False, "code": 'dim' },			# kick-l-25[srcch3]
    ('kick-r', 'KICK_R'): {"keep": True, "code": 'dim' },			# kick-r-25[srcch4]
    # ('kick-r', 'KICK_TRIGGER'): {"keep": False, "code": 'tim' },			# kick-trg-25[srcch5]
    ('kick-r', 'OH'): {"keep": True, "code": 'ons' },			# oh-l-25[srcch6] + oh-r-25[srcch7]
    # ('kick-r', 'RIDE'): {"keep": False, "code": 'dtm' },			# ride-25[srcch8]
    # ('kick-r', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# snare-btm-25[srcch9]
    # ('kick-r', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# snare-top-25[srcch10]
    # ('kick-r', 'TOM1'): {"keep": False, "code": 'dtm' },			# tom1-25[srcch11]
    # ('kick-r', 'TOM2'): {"keep": False, "code": 'dtm' },			# tom2-25[srcch12]
    # ('kick-r', 'TOM3'): {"keep": False, "code": 'dtm' },			# tom3-25[srcch13]
    # ('kick-r', 'TOM4'): {"keep": False, "code": 'dtm' },			# tom4-25[srcch14]

    # Microphone groups for ride.
    ('ride', 'AMB'): {"keep": True, "code": 'rfs' },			# amb-l-21[srcch0] + amb-r-21[srcch1]
    # ('ride', 'HIHAT'): {"keep": False, "code": 'dtm' },			# hihat-21[srcch2]
    # ('ride', 'KICK_L'): {"keep": False, "code": 'dim' },			# kick-l-21[srcch3]
    # ('ride', 'KICK_R'): {"keep": False, "code": 'dim' },			# kick-r-21[srcch4]
    # ('ride', 'KICK_TRIGGER'): {"keep": False, "code": 'tim' },			# kick-trg-21[srcch5]
    ('ride', 'OH'): {"keep": True, "code": 'ons' },			# oh-l-21[srcch6] + oh-r-21[srcch7]
    ('ride', 'RIDE'): {"keep": True, "code": 'dtm' },			# ride-21[srcch8]
    # ('ride', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# snare-btm-21[srcch9]
    # ('ride', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# snare-top-21[srcch10]
    # ('ride', 'TOM1'): {"keep": False, "code": 'dtm' },			# tom1-21[srcch11]
    # ('ride', 'TOM2'): {"keep": False, "code": 'dtm' },			# tom2-21[srcch12]
    # ('ride', 'TOM3'): {"keep": False, "code": 'dtm' },			# tom3-21[srcch13]
    # ('ride', 'TOM4'): {"keep": False, "code": 'dtm' },			# tom4-21[srcch14]

    # Microphone groups for ride-bell.
    ('ride-bell', 'AMB'): {"keep": True, "code": 'rfs' },			# amb-l-22[srcch0] + amb-r-22[srcch1]
    # ('ride-bell', 'HIHAT'): {"keep": False, "code": 'dtm' },			# hihat-22[srcch2]
    # ('ride-bell', 'KICK_L'): {"keep": False, "code": 'dim' },			# kick-l-22[srcch3]
    # ('ride-bell', 'KICK_R'): {"keep": False, "code": 'dim' },			# kick-r-22[srcch4]
    # ('ride-bell', 'KICK_TRIGGER'): {"keep": False, "code": 'tim' },			# kick-trg-22[srcch5]
    ('ride-bell', 'OH'): {"keep": True, "code": 'ons' },			# oh-l-22[srcch6] + oh-r-22[srcch7]
    ('ride-bell', 'RIDE'): {"keep": True, "code": 'dtm' },			# ride-22[srcch8]
    # ('ride-bell', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# snare-btm-22[srcch9]
    # ('ride-bell', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# snare-top-22[srcch10]
    # ('ride-bell', 'TOM1'): {"keep": False, "code": 'dtm' },			# tom1-22[srcch11]
    # ('ride-bell', 'TOM2'): {"keep": False, "code": 'dtm' },			# tom2-22[srcch12]
    # ('ride-bell', 'TOM3'): {"keep": False, "code": 'dtm' },			# tom3-22[srcch13]
    # ('ride-bell', 'TOM4'): {"keep": False, "code": 'dtm' },			# tom4-22[srcch14]

    # Microphone groups for snare-artstar.
    ('snare-artstar', 'AMB'): {"keep": True, "code": 'rfs' },			# amb-l-1[srcch0] + amb-r-1[srcch1]
    # ('snare-artstar', 'HIHAT'): {"keep": False, "code": 'dtm' },			# hihat-1[srcch2]
    # ('snare-artstar', 'KICK_L'): {"keep": False, "code": 'dim' },			# kick-l-1[srcch3]
    # ('snare-artstar', 'KICK_R'): {"keep": False, "code": 'dim' },			# kick-r-1[srcch4]
    # ('snare-artstar', 'KICK_TRIGGER'): {"keep": False, "code": 'tim' },			# kick-trg-1[srcch5]
    ('snare-artstar', 'OH'): {"keep": True, "code": 'ons' },			# oh-l-1[srcch6] + oh-r-1[srcch7]
    # ('snare-artstar', 'RIDE'): {"keep": False, "code": 'dtm' },			# ride-1[srcch8]
    ('snare-artstar', 'SNARE_BOTTOM'): {"keep": True, "code": 'dbm' },			# snare-btm-1[srcch9]
    ('snare-artstar', 'SNARE_TOP'): {"keep": True, "code": 'dtm' },			# snare-top-1[srcch10]
    # ('snare-artstar', 'TOM1'): {"keep": False, "code": 'dtm' },			# tom1-1[srcch11]
    # ('snare-artstar', 'TOM2'): {"keep": False, "code": 'dtm' },			# tom2-1[srcch12]
    # ('snare-artstar', 'TOM3'): {"keep": False, "code": 'dtm' },			# tom3-1[srcch13]
    # ('snare-artstar', 'TOM4'): {"keep": False, "code": 'dtm' },			# tom4-1[srcch14]

    # Microphone groups for snare-ludwig.
    ('snare-ludwig', 'AMB'): {"keep": True, "code": 'rfs' },			# amb-l-2[srcch0] + amb-r-2[srcch1]
    # ('snare-ludwig', 'HIHAT'): {"keep": False, "code": 'dtm' },			# hihat-2[srcch2]
    # ('snare-ludwig', 'KICK_L'): {"keep": False, "code": 'dim' },			# kick-l-2[srcch3]
    # ('snare-ludwig', 'KICK_R'): {"keep": False, "code": 'dim' },			# kick-r-2[srcch4]
    # ('snare-ludwig', 'KICK_TRIGGER'): {"keep": False, "code": 'tim' },			# kick-trg-2[srcch5]
    ('snare-ludwig', 'OH'): {"keep": True, "code": 'ons' },			# oh-l-2[srcch6] + oh-r-2[srcch7]
    # ('snare-ludwig', 'RIDE'): {"keep": False, "code": 'dtm' },			# ride-2[srcch8]
    ('snare-ludwig', 'SNARE_BOTTOM'): {"keep": True, "code": 'dbm' },			# snare-btm-2[srcch9]
    ('snare-ludwig', 'SNARE_TOP'): {"keep": True, "code": 'dtm' },			# snare-top-2[srcch10]
    # ('snare-ludwig', 'TOM1'): {"keep": False, "code": 'dtm' },			# tom1-2[srcch11]
    # ('snare-ludwig', 'TOM2'): {"keep": False, "code": 'dtm' },			# tom2-2[srcch12]
    # ('snare-ludwig', 'TOM3'): {"keep": False, "code": 'dtm' },			# tom3-2[srcch13]
    # ('snare-ludwig', 'TOM4'): {"keep": False, "code": 'dtm' },			# tom4-2[srcch14]

    # Microphone groups for stagg-12inch.
    ('stagg-12inch', 'AMB'): {"keep": True, "code": 'rfs' },			# amb-l-12[srcch0] + amb-r-12[srcch1]
    # ('stagg-12inch', 'HIHAT'): {"keep": False, "code": 'dtm' },			# hihat-12[srcch2]
    # ('stagg-12inch', 'KICK_L'): {"keep": False, "code": 'dim' },			# kick-l-12[srcch3]
    # ('stagg-12inch', 'KICK_R'): {"keep": False, "code": 'dim' },			# kick-r-12[srcch4]
    # ('stagg-12inch', 'KICK_TRIGGER'): {"keep": False, "code": 'tim' },			# kick-trg-12[srcch5]
    ('stagg-12inch', 'OH'): {"keep": True, "code": 'ons' },			# oh-l-12[srcch6] + oh-r-12[srcch7]
    # ('stagg-12inch', 'RIDE'): {"keep": False, "code": 'dtm' },			# ride-12[srcch8]
    # ('stagg-12inch', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# snare-btm-12[srcch9]
    # ('stagg-12inch', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# snare-top-12[srcch10]
    # ('stagg-12inch', 'TOM1'): {"keep": False, "code": 'dtm' },			# tom1-12[srcch11]
    # ('stagg-12inch', 'TOM2'): {"keep": False, "code": 'dtm' },			# tom2-12[srcch12]
    # ('stagg-12inch', 'TOM3'): {"keep": False, "code": 'dtm' },			# tom3-12[srcch13]
    # ('stagg-12inch', 'TOM4'): {"keep": False, "code": 'dtm' },			# tom4-12[srcch14]

    # Microphone groups for stagg-8inch.
    ('stagg-8inch', 'AMB'): {"keep": True, "code": 'rfs' },			# amb-l-10[srcch0] + amb-r-10[srcch1]
    # ('stagg-8inch', 'HIHAT'): {"keep": False, "code": 'dtm' },			# hihat-10[srcch2]
    # ('stagg-8inch', 'KICK_L'): {"keep": False, "code": 'dim' },			# kick-l-10[srcch3]
    # ('stagg-8inch', 'KICK_R'): {"keep": False, "code": 'dim' },			# kick-r-10[srcch4]
    # ('stagg-8inch', 'KICK_TRIGGER'): {"keep": False, "code": 'tim' },			# kick-trg-10[srcch5]
    ('stagg-8inch', 'OH'): {"keep": True, "code": 'ons' },			# oh-l-10[srcch6] + oh-r-10[srcch7]
    # ('stagg-8inch', 'RIDE'): {"keep": False, "code": 'dtm' },			# ride-10[srcch8]
    # ('stagg-8inch', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# snare-btm-10[srcch9]
    # ('stagg-8inch', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# snare-top-10[srcch10]
    # ('stagg-8inch', 'TOM1'): {"keep": False, "code": 'dtm' },			# tom1-10[srcch11]
    # ('stagg-8inch', 'TOM2'): {"keep": False, "code": 'dtm' },			# tom2-10[srcch12]
    # ('stagg-8inch', 'TOM3'): {"keep": False, "code": 'dtm' },			# tom3-10[srcch13]
    # ('stagg-8inch', 'TOM4'): {"keep": False, "code": 'dtm' },			# tom4-10[srcch14]

    # Microphone groups for tom1.
    ('tom1', 'AMB'): {"keep": True, "code": 'rfs' },			# amb-l-16[srcch0] + amb-r-16[srcch1]
    # ('tom1', 'HIHAT'): {"keep": False, "code": 'dtm' },			# hihat-16[srcch2]
    # ('tom1', 'KICK_L'): {"keep": False, "code": 'dim' },			# kick-l-16[srcch3]
    # ('tom1', 'KICK_R'): {"keep": False, "code": 'dim' },			# kick-r-16[srcch4]
    # ('tom1', 'KICK_TRIGGER'): {"keep": False, "code": 'tim' },			# kick-trg-16[srcch5]
    ('tom1', 'OH'): {"keep": True, "code": 'ons' },			# oh-l-16[srcch6] + oh-r-16[srcch7]
    # ('tom1', 'RIDE'): {"keep": False, "code": 'dtm' },			# ride-16[srcch8]
    # ('tom1', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# snare-btm-16[srcch9]
    # ('tom1', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# snare-top-16[srcch10]
    ('tom1', 'TOM1'): {"keep": True, "code": 'dtm' },			# tom1-16[srcch11]
    # ('tom1', 'TOM2'): {"keep": False, "code": 'dtm' },			# tom2-16[srcch12]
    # ('tom1', 'TOM3'): {"keep": False, "code": 'dtm' },			# tom3-16[srcch13]
    # ('tom1', 'TOM4'): {"keep": False, "code": 'dtm' },			# tom4-16[srcch14]

    # Microphone groups for tom2.
    ('tom2', 'AMB'): {"keep": True, "code": 'rfs' },			# amb-l-17[srcch0] + amb-r-17[srcch1]
    # ('tom2', 'HIHAT'): {"keep": False, "code": 'dtm' },			# hihat-17[srcch2]
    # ('tom2', 'KICK_L'): {"keep": False, "code": 'dim' },			# kick-l-17[srcch3]
    # ('tom2', 'KICK_R'): {"keep": False, "code": 'dim' },			# kick-r-17[srcch4]
    # ('tom2', 'KICK_TRIGGER'): {"keep": False, "code": 'tim' },			# kick-trg-17[srcch5]
    ('tom2', 'OH'): {"keep": True, "code": 'ons' },			# oh-l-17[srcch6] + oh-r-17[srcch7]
    # ('tom2', 'RIDE'): {"keep": False, "code": 'dtm' },			# ride-17[srcch8]
    # ('tom2', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# snare-btm-17[srcch9]
    # ('tom2', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# snare-top-17[srcch10]
    # ('tom2', 'TOM1'): {"keep": False, "code": 'dtm' },			# tom1-17[srcch11]
    ('tom2', 'TOM2'): {"keep": True, "code": 'dtm' },			# tom2-17[srcch12]
    # ('tom2', 'TOM3'): {"keep": False, "code": 'dtm' },			# tom3-17[srcch13]
    # ('tom2', 'TOM4'): {"keep": False, "code": 'dtm' },			# tom4-17[srcch14]

    # Microphone groups for tom3.
    ('tom3', 'AMB'): {"keep": True, "code": 'rfs' },			# amb-l-18[srcch0] + amb-r-18[srcch1]
    # ('tom3', 'HIHAT'): {"keep": False, "code": 'dtm' },			# hihat-18[srcch2]
    # ('tom3', 'KICK_L'): {"keep": False, "code": 'dim' },			# kick-l-18[srcch3]
    # ('tom3', 'KICK_R'): {"keep": False, "code": 'dim' },			# kick-r-18[srcch4]
    # ('tom3', 'KICK_TRIGGER'): {"keep": False, "code": 'tim' },			# kick-trg-18[srcch5]
    ('tom3', 'OH'): {"keep": True, "code": 'ons' },			# oh-l-18[srcch6] + oh-r-18[srcch7]
    # ('tom3', 'RIDE'): {"keep": False, "code": 'dtm' },			# ride-18[srcch8]
    # ('tom3', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# snare-btm-18[srcch9]
    # ('tom3', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# snare-top-18[srcch10]
    # ('tom3', 'TOM1'): {"keep": False, "code": 'dtm' },			# tom1-18[srcch11]
    # ('tom3', 'TOM2'): {"keep": False, "code": 'dtm' },			# tom2-18[srcch12]
    ('tom3', 'TOM3'): {"keep": True, "code": 'dtm' },			# tom3-18[srcch13]
    # ('tom3', 'TOM4'): {"keep": False, "code": 'dtm' },			# tom4-18[srcch14]

    # Microphone groups for tom4.
    ('tom4', 'AMB'): {"keep": True, "code": 'rfs' },			# amb-l-19[srcch0] + amb-r-19[srcch1]
    # ('tom4', 'HIHAT'): {"keep": False, "code": 'dtm' },			# hihat-19[srcch2]
    # ('tom4', 'KICK_L'): {"keep": False, "code": 'dim' },			# kick-l-19[srcch3]
    # ('tom4', 'KICK_R'): {"keep": False, "code": 'dim' },			# kick-r-19[srcch4]
    # ('tom4', 'KICK_TRIGGER'): {"keep": False, "code": 'tim' },			# kick-trg-19[srcch5]
    ('tom4', 'OH'): {"keep": True, "code": 'ons' },			# oh-l-19[srcch6] + oh-r-19[srcch7]
    # ('tom4', 'RIDE'): {"keep": False, "code": 'dtm' },			# ride-19[srcch8]
    # ('tom4', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# snare-btm-19[srcch9]
    # ('tom4', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# snare-top-19[srcch10]
    # ('tom4', 'TOM1'): {"keep": False, "code": 'dtm' },			# tom1-19[srcch11]
    # ('tom4', 'TOM2'): {"keep": False, "code": 'dtm' },			# tom2-19[srcch12]
    # ('tom4', 'TOM3'): {"keep": False, "code": 'dtm' },			# tom3-19[srcch13]
    ('tom4', 'TOM4'): {"keep": True, "code": 'dtm' },			# tom4-19[srcch14]

    # Microphone groups for zilbel.
    ('zilbel', 'AMB'): {"keep": True, "code": 'rfs' },			# amb-l-24[srcch0] + amb-r-24[srcch1]
    # ('zilbel', 'HIHAT'): {"keep": False, "code": 'dtm' },			# hihat-24[srcch2]
    # ('zilbel', 'KICK_L'): {"keep": False, "code": 'dim' },			# kick-l-24[srcch3]
    # ('zilbel', 'KICK_R'): {"keep": False, "code": 'dim' },			# kick-r-24[srcch4]
    # ('zilbel', 'KICK_TRIGGER'): {"keep": False, "code": 'tim' },			# kick-trg-24[srcch5]
    ('zilbel', 'OH'): {"keep": True, "code": 'ons' },			# oh-l-24[srcch6] + oh-r-24[srcch7]
    # ('zilbel', 'RIDE'): {"keep": False, "code": 'dtm' },			# ride-24[srcch8]
    # ('zilbel', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# snare-btm-24[srcch9]
    # ('zilbel', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# snare-top-24[srcch10]
    # ('zilbel', 'TOM1'): {"keep": False, "code": 'dtm' },			# tom1-24[srcch11]
    # ('zilbel', 'TOM2'): {"keep": False, "code": 'dtm' },			# tom2-24[srcch12]
    # ('zilbel', 'TOM3'): {"keep": False, "code": 'dtm' },			# tom3-24[srcch13]
    # ('zilbel', 'TOM4'): {"keep": False, "code": 'dtm' },			# tom4-24[srcch14]

}

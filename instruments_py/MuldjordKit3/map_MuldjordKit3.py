# Auto-generated review map for MuldjordKit3.
# Edit values here, then run the corresponding convert_*.py script.

INSTRUMENTS = {
    'China': {
        "instrument": 'china-X1',
        "articulation": 'edge',
        "parameter": '',
    },
    'CrashL': {
        "instrument": 'crash-L1',
        "articulation": 'edge',
        "parameter": '',
    },
    'CrashR': {
        "instrument": 'crash-R1',
        "articulation": 'edge',
        "parameter": '',
    },
    'HihatClosed': {
        "instrument": 'hihat-X1',
        "articulation": 'bow[close]',
        "parameter": '-pdl_opn_cls078',
    },
    'HihatOpen': {
        "instrument": 'hihat-X1',
        "articulation": 'edge[open]',
        "parameter": '-pdl_opn_cls043',
    },
    'KdrumL': {
        "instrument": 'kick-L1',
        "articulation": 'center',
        "parameter": '',
    },
    'KdrumR': {
        "instrument": 'kick-R1',
        "articulation": 'center',
        "parameter": '',
    },
    'RideL': {
        "instrument": 'ride-L1',
        "articulation": 'bow',
        "parameter": '',
    },
    'RideLBell': {
        "instrument": 'ride-L1',
        "articulation": 'bell',
        "parameter": '',
    },
    'RideR': {
        "instrument": 'ride-R1',
        "articulation": 'bow',
        "parameter": '',
    },
    'RideRBell': {
        "instrument": 'ride-R1',
        "articulation": 'bell',
        "parameter": '',
    },
    'Snare': {
        "instrument": 'snare-X1',
        "articulation": 'center',
        "parameter": '',
    },
    'SnareRest': {
        "instrument": 'snare-X1',
        "articulation": 'rest',
        "parameter": '',
    },
    'Tom1': {
        "instrument": 'tom-X1',
        "articulation": 'center',
        "parameter": '',
    },
    'Tom2': {
        "instrument": 'tom-X2',
        "articulation": 'center',
        "parameter": '',
    },
    'Tom3': {
        "instrument": 'tom-X3',
        "articulation": 'center',
        "parameter": '',
    },
    'Tom4': {
        "instrument": 'tom-X4',
        "articulation": 'center',
        "parameter": '',
    },
}

MICS = {
    # Microphone groups for China.
    ('China', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbL[srcch0] + AmbR[srcch1]
    # ('China', 'DTM'): {"keep": False, "code": 'dtm' },			# Trigger[srcch15]
    # ('China', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch2]
    # ('China', 'KDRUML'): {"keep": False, "code": 'dim' },			# KdrumL[srcch3]
    # ('China', 'KDRUMR'): {"keep": False, "code": 'dim' },			# KdrumR[srcch4]
    ('China', 'OH'): {"keep": True, "code": 'ons' },			# OHL[srcch5] + OHR[srcch6]
    # ('China', 'RIDEL'): {"keep": False, "code": 'dtm' },			# RideL[srcch7]
    # ('China', 'RIDER'): {"keep": False, "code": 'dtm' },			# RideR[srcch8]
    # ('China', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# Snare_bottom[srcch9]
    # ('China', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# Snare_top[srcch10]
    # ('China', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch11]
    # ('China', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch12]
    # ('China', 'TOM3'): {"keep": False, "code": 'dtm' },			# Tom3[srcch13]
    # ('China', 'TOM4'): {"keep": False, "code": 'dtm' },			# Tom4[srcch14]

    # Microphone groups for CrashL.
    ('CrashL', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbL[srcch0] + AmbR[srcch1]
    # ('CrashL', 'DTM'): {"keep": False, "code": 'dtm' },			# Trigger[srcch15]
    # ('CrashL', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch2]
    # ('CrashL', 'KDRUML'): {"keep": False, "code": 'dim' },			# KdrumL[srcch3]
    # ('CrashL', 'KDRUMR'): {"keep": False, "code": 'dim' },			# KdrumR[srcch4]
    ('CrashL', 'OH'): {"keep": True, "code": 'ons' },			# OHL[srcch5] + OHR[srcch6]
    # ('CrashL', 'RIDEL'): {"keep": False, "code": 'dtm' },			# RideL[srcch7]
    # ('CrashL', 'RIDER'): {"keep": False, "code": 'dtm' },			# RideR[srcch8]
    # ('CrashL', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# Snare_bottom[srcch9]
    # ('CrashL', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# Snare_top[srcch10]
    # ('CrashL', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch11]
    # ('CrashL', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch12]
    # ('CrashL', 'TOM3'): {"keep": False, "code": 'dtm' },			# Tom3[srcch13]
    # ('CrashL', 'TOM4'): {"keep": False, "code": 'dtm' },			# Tom4[srcch14]

    # Microphone groups for CrashR.
    ('CrashR', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbL[srcch0] + AmbR[srcch1]
    # ('CrashR', 'DTM'): {"keep": False, "code": 'dtm' },			# Trigger[srcch15]
    # ('CrashR', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch2]
    # ('CrashR', 'KDRUML'): {"keep": False, "code": 'dim' },			# KdrumL[srcch3]
    # ('CrashR', 'KDRUMR'): {"keep": False, "code": 'dim' },			# KdrumR[srcch4]
    ('CrashR', 'OH'): {"keep": True, "code": 'ons' },			# OHL[srcch5] + OHR[srcch6]
    # ('CrashR', 'RIDEL'): {"keep": False, "code": 'dtm' },			# RideL[srcch7]
    # ('CrashR', 'RIDER'): {"keep": False, "code": 'dtm' },			# RideR[srcch8]
    # ('CrashR', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# Snare_bottom[srcch9]
    # ('CrashR', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# Snare_top[srcch10]
    # ('CrashR', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch11]
    # ('CrashR', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch12]
    # ('CrashR', 'TOM3'): {"keep": False, "code": 'dtm' },			# Tom3[srcch13]
    # ('CrashR', 'TOM4'): {"keep": False, "code": 'dtm' },			# Tom4[srcch14]

    # Microphone groups for HihatClosed.
    ('HihatClosed', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbL[srcch0] + AmbR[srcch1]
    # ('HihatClosed', 'DTM'): {"keep": False, "code": 'dtm' },			# Trigger[srcch15]
    ('HihatClosed', 'HIHAT'): {"keep": True, "code": 'dtm' },			# Hihat[srcch2]
    # ('HihatClosed', 'KDRUML'): {"keep": False, "code": 'dim' },			# KdrumL[srcch3]
    # ('HihatClosed', 'KDRUMR'): {"keep": False, "code": 'dim' },			# KdrumR[srcch4]
    ('HihatClosed', 'OH'): {"keep": True, "code": 'ons' },			# OHL[srcch5] + OHR[srcch6]
    # ('HihatClosed', 'RIDEL'): {"keep": False, "code": 'dtm' },			# RideL[srcch7]
    # ('HihatClosed', 'RIDER'): {"keep": False, "code": 'dtm' },			# RideR[srcch8]
    # ('HihatClosed', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# Snare_bottom[srcch9]
    # ('HihatClosed', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# Snare_top[srcch10]
    # ('HihatClosed', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch11]
    # ('HihatClosed', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch12]
    # ('HihatClosed', 'TOM3'): {"keep": False, "code": 'dtm' },			# Tom3[srcch13]
    # ('HihatClosed', 'TOM4'): {"keep": False, "code": 'dtm' },			# Tom4[srcch14]

    # Microphone groups for HihatOpen.
    ('HihatOpen', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbL[srcch0] + AmbR[srcch1]
    # ('HihatOpen', 'DTM'): {"keep": False, "code": 'dtm' },			# Trigger[srcch15]
    ('HihatOpen', 'HIHAT'): {"keep": True, "code": 'dtm' },			# Hihat[srcch2]
    # ('HihatOpen', 'KDRUML'): {"keep": False, "code": 'dim' },			# KdrumL[srcch3]
    # ('HihatOpen', 'KDRUMR'): {"keep": False, "code": 'dim' },			# KdrumR[srcch4]
    ('HihatOpen', 'OH'): {"keep": True, "code": 'ons' },			# OHL[srcch5] + OHR[srcch6]
    # ('HihatOpen', 'RIDEL'): {"keep": False, "code": 'dtm' },			# RideL[srcch7]
    # ('HihatOpen', 'RIDER'): {"keep": False, "code": 'dtm' },			# RideR[srcch8]
    # ('HihatOpen', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# Snare_bottom[srcch9]
    # ('HihatOpen', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# Snare_top[srcch10]
    # ('HihatOpen', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch11]
    # ('HihatOpen', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch12]
    # ('HihatOpen', 'TOM3'): {"keep": False, "code": 'dtm' },			# Tom3[srcch13]
    # ('HihatOpen', 'TOM4'): {"keep": False, "code": 'dtm' },			# Tom4[srcch14]

    # Microphone groups for KdrumL.
    ('KdrumL', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbL[srcch0] + AmbR[srcch1]
    # ('KdrumL', 'DTM'): {"keep": False, "code": 'dtm' },			# Trigger[srcch15]
    # ('KdrumL', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch2]
    ('KdrumL', 'KDRUML'): {"keep": True, "code": 'dim' },			# KdrumL[srcch3]
    ('KdrumL', 'KDRUMR'): {"keep": False, "code": 'dim' },			# KdrumR[srcch4]
    ('KdrumL', 'OH'): {"keep": True, "code": 'ons' },			# OHL[srcch5] + OHR[srcch6]
    # ('KdrumL', 'RIDEL'): {"keep": False, "code": 'dtm' },			# RideL[srcch7]
    # ('KdrumL', 'RIDER'): {"keep": False, "code": 'dtm' },			# RideR[srcch8]
    # ('KdrumL', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# Snare_bottom[srcch9]
    # ('KdrumL', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# Snare_top[srcch10]
    # ('KdrumL', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch11]
    # ('KdrumL', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch12]
    # ('KdrumL', 'TOM3'): {"keep": False, "code": 'dtm' },			# Tom3[srcch13]
    # ('KdrumL', 'TOM4'): {"keep": False, "code": 'dtm' },			# Tom4[srcch14]

    # Microphone groups for KdrumR.
    ('KdrumR', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbL[srcch0] + AmbR[srcch1]
    # ('KdrumR', 'DTM'): {"keep": False, "code": 'dtm' },			# Trigger[srcch15]
    # ('KdrumR', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch2]
    ('KdrumR', 'KDRUML'): {"keep": False, "code": 'dim' },			# KdrumL[srcch3]
    ('KdrumR', 'KDRUMR'): {"keep": True, "code": 'dim' },			# KdrumR[srcch4]
    ('KdrumR', 'OH'): {"keep": True, "code": 'ons' },			# OHL[srcch5] + OHR[srcch6]
    # ('KdrumR', 'RIDEL'): {"keep": False, "code": 'dtm' },			# RideL[srcch7]
    # ('KdrumR', 'RIDER'): {"keep": False, "code": 'dtm' },			# RideR[srcch8]
    # ('KdrumR', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# Snare_bottom[srcch9]
    # ('KdrumR', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# Snare_top[srcch10]
    # ('KdrumR', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch11]
    # ('KdrumR', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch12]
    # ('KdrumR', 'TOM3'): {"keep": False, "code": 'dtm' },			# Tom3[srcch13]
    # ('KdrumR', 'TOM4'): {"keep": False, "code": 'dtm' },			# Tom4[srcch14]

    # Microphone groups for RideL.
    ('RideL', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbL[srcch0] + AmbR[srcch1]
    # ('RideL', 'DTM'): {"keep": False, "code": 'dtm' },			# Trigger[srcch15]
    # ('RideL', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch2]
    # ('RideL', 'KDRUML'): {"keep": False, "code": 'dim' },			# KdrumL[srcch3]
    # ('RideL', 'KDRUMR'): {"keep": False, "code": 'dim' },			# KdrumR[srcch4]
    ('RideL', 'OH'): {"keep": True, "code": 'ons' },			# OHL[srcch5] + OHR[srcch6]
    ('RideL', 'RIDEL'): {"keep": True, "code": 'dtm' },			# RideL[srcch7]
    # ('RideL', 'RIDER'): {"keep": False, "code": 'dtm' },			# RideR[srcch8]
    # ('RideL', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# Snare_bottom[srcch9]
    # ('RideL', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# Snare_top[srcch10]
    # ('RideL', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch11]
    # ('RideL', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch12]
    # ('RideL', 'TOM3'): {"keep": False, "code": 'dtm' },			# Tom3[srcch13]
    # ('RideL', 'TOM4'): {"keep": False, "code": 'dtm' },			# Tom4[srcch14]

    # Microphone groups for RideLBell.
    ('RideLBell', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbL[srcch0] + AmbR[srcch1]
    # ('RideLBell', 'DTM'): {"keep": False, "code": 'dtm' },			# Trigger[srcch15]
    # ('RideLBell', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch2]
    # ('RideLBell', 'KDRUML'): {"keep": False, "code": 'dim' },			# KdrumL[srcch3]
    # ('RideLBell', 'KDRUMR'): {"keep": False, "code": 'dim' },			# KdrumR[srcch4]
    ('RideLBell', 'OH'): {"keep": True, "code": 'ons' },			# OHL[srcch5] + OHR[srcch6]
    ('RideLBell', 'RIDEL'): {"keep": True, "code": 'dtm' },			# RideL[srcch7]
    # ('RideLBell', 'RIDER'): {"keep": False, "code": 'dtm' },			# RideR[srcch8]
    # ('RideLBell', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# Snare_bottom[srcch9]
    # ('RideLBell', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# Snare_top[srcch10]
    # ('RideLBell', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch11]
    # ('RideLBell', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch12]
    # ('RideLBell', 'TOM3'): {"keep": False, "code": 'dtm' },			# Tom3[srcch13]
    # ('RideLBell', 'TOM4'): {"keep": False, "code": 'dtm' },			# Tom4[srcch14]

    # Microphone groups for RideR.
    ('RideR', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbL[srcch0] + AmbR[srcch1]
    # ('RideR', 'DTM'): {"keep": False, "code": 'dtm' },			# Trigger[srcch15]
    # ('RideR', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch2]
    # ('RideR', 'KDRUML'): {"keep": False, "code": 'dim' },			# KdrumL[srcch3]
    # ('RideR', 'KDRUMR'): {"keep": False, "code": 'dim' },			# KdrumR[srcch4]
    ('RideR', 'OH'): {"keep": True, "code": 'ons' },			# OHL[srcch5] + OHR[srcch6]
    # ('RideR', 'RIDEL'): {"keep": False, "code": 'dtm' },			# RideL[srcch7]
    ('RideR', 'RIDER'): {"keep": True, "code": 'dtm' },			# RideR[srcch8]
    # ('RideR', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# Snare_bottom[srcch9]
    # ('RideR', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# Snare_top[srcch10]
    # ('RideR', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch11]
    # ('RideR', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch12]
    # ('RideR', 'TOM3'): {"keep": False, "code": 'dtm' },			# Tom3[srcch13]
    # ('RideR', 'TOM4'): {"keep": False, "code": 'dtm' },			# Tom4[srcch14]

    # Microphone groups for RideRBell.
    ('RideRBell', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbL[srcch0] + AmbR[srcch1]
    # ('RideRBell', 'DTM'): {"keep": False, "code": 'dtm' },			# Trigger[srcch15]
    # ('RideRBell', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch2]
    # ('RideRBell', 'KDRUML'): {"keep": False, "code": 'dim' },			# KdrumL[srcch3]
    # ('RideRBell', 'KDRUMR'): {"keep": False, "code": 'dim' },			# KdrumR[srcch4]
    ('RideRBell', 'OH'): {"keep": True, "code": 'ons' },			# OHL[srcch5] + OHR[srcch6]
    # ('RideRBell', 'RIDEL'): {"keep": False, "code": 'dtm' },			# RideL[srcch7]
    ('RideRBell', 'RIDER'): {"keep": True, "code": 'dtm' },			# RideR[srcch8]
    # ('RideRBell', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# Snare_bottom[srcch9]
    # ('RideRBell', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# Snare_top[srcch10]
    # ('RideRBell', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch11]
    # ('RideRBell', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch12]
    # ('RideRBell', 'TOM3'): {"keep": False, "code": 'dtm' },			# Tom3[srcch13]
    # ('RideRBell', 'TOM4'): {"keep": False, "code": 'dtm' },			# Tom4[srcch14]

    # Microphone groups for Snare.
    ('Snare', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbL[srcch0] + AmbR[srcch1]
    # ('Snare', 'DTM'): {"keep": False, "code": 'dtm' },			# Trigger[srcch15]
    # ('Snare', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch2]
    # ('Snare', 'KDRUML'): {"keep": False, "code": 'dim' },			# KdrumL[srcch3]
    # ('Snare', 'KDRUMR'): {"keep": False, "code": 'dim' },			# KdrumR[srcch4]
    ('Snare', 'OH'): {"keep": True, "code": 'ons' },			# OHL[srcch5] + OHR[srcch6]
    # ('Snare', 'RIDEL'): {"keep": False, "code": 'dtm' },			# RideL[srcch7]
    # ('Snare', 'RIDER'): {"keep": False, "code": 'dtm' },			# RideR[srcch8]
    ('Snare', 'SNARE_BOTTOM'): {"keep": True, "code": 'dbm' },			# Snare_bottom[srcch9]
    ('Snare', 'SNARE_TOP'): {"keep": True, "code": 'dtm' },			# Snare_top[srcch10]
    # ('Snare', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch11]
    # ('Snare', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch12]
    # ('Snare', 'TOM3'): {"keep": False, "code": 'dtm' },			# Tom3[srcch13]
    # ('Snare', 'TOM4'): {"keep": False, "code": 'dtm' },			# Tom4[srcch14]

    # Microphone groups for SnareRest.
    ('SnareRest', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbL[srcch0] + AmbR[srcch1]
    # ('SnareRest', 'DTM'): {"keep": False, "code": 'dtm' },			# Trigger[srcch15]
    # ('SnareRest', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch2]
    # ('SnareRest', 'KDRUML'): {"keep": False, "code": 'dim' },			# KdrumL[srcch3]
    # ('SnareRest', 'KDRUMR'): {"keep": False, "code": 'dim' },			# KdrumR[srcch4]
    ('SnareRest', 'OH'): {"keep": True, "code": 'ons' },			# OHL[srcch5] + OHR[srcch6]
    # ('SnareRest', 'RIDEL'): {"keep": False, "code": 'dtm' },			# RideL[srcch7]
    # ('SnareRest', 'RIDER'): {"keep": False, "code": 'dtm' },			# RideR[srcch8]
    ('SnareRest', 'SNARE_BOTTOM'): {"keep": True, "code": 'dbm' },			# Snare_bottom[srcch9]
    ('SnareRest', 'SNARE_TOP'): {"keep": True, "code": 'dtm' },			# Snare_top[srcch10]
    # ('SnareRest', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch11]
    # ('SnareRest', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch12]
    # ('SnareRest', 'TOM3'): {"keep": False, "code": 'dtm' },			# Tom3[srcch13]
    # ('SnareRest', 'TOM4'): {"keep": False, "code": 'dtm' },			# Tom4[srcch14]

    # Microphone groups for Tom1.
    ('Tom1', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbL[srcch0] + AmbR[srcch1]
    # ('Tom1', 'DTM'): {"keep": False, "code": 'dtm' },			# Trigger[srcch15]
    # ('Tom1', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch2]
    # ('Tom1', 'KDRUML'): {"keep": False, "code": 'dim' },			# KdrumL[srcch3]
    # ('Tom1', 'KDRUMR'): {"keep": False, "code": 'dim' },			# KdrumR[srcch4]
    ('Tom1', 'OH'): {"keep": True, "code": 'ons' },			# OHL[srcch5] + OHR[srcch6]
    # ('Tom1', 'RIDEL'): {"keep": False, "code": 'dtm' },			# RideL[srcch7]
    # ('Tom1', 'RIDER'): {"keep": False, "code": 'dtm' },			# RideR[srcch8]
    # ('Tom1', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# Snare_bottom[srcch9]
    # ('Tom1', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# Snare_top[srcch10]
    ('Tom1', 'TOM1'): {"keep": True, "code": 'dtm' },			# Tom1[srcch11]
    # ('Tom1', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch12]
    # ('Tom1', 'TOM3'): {"keep": False, "code": 'dtm' },			# Tom3[srcch13]
    # ('Tom1', 'TOM4'): {"keep": False, "code": 'dtm' },			# Tom4[srcch14]

    # Microphone groups for Tom2.
    ('Tom2', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbL[srcch0] + AmbR[srcch1]
    # ('Tom2', 'DTM'): {"keep": False, "code": 'dtm' },			# Trigger[srcch15]
    # ('Tom2', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch2]
    # ('Tom2', 'KDRUML'): {"keep": False, "code": 'dim' },			# KdrumL[srcch3]
    # ('Tom2', 'KDRUMR'): {"keep": False, "code": 'dim' },			# KdrumR[srcch4]
    ('Tom2', 'OH'): {"keep": True, "code": 'ons' },			# OHL[srcch5] + OHR[srcch6]
    # ('Tom2', 'RIDEL'): {"keep": False, "code": 'dtm' },			# RideL[srcch7]
    # ('Tom2', 'RIDER'): {"keep": False, "code": 'dtm' },			# RideR[srcch8]
    # ('Tom2', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# Snare_bottom[srcch9]
    # ('Tom2', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# Snare_top[srcch10]
    # ('Tom2', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch11]
    ('Tom2', 'TOM2'): {"keep": True, "code": 'dtm' },			# Tom2[srcch12]
    # ('Tom2', 'TOM3'): {"keep": False, "code": 'dtm' },			# Tom3[srcch13]
    # ('Tom2', 'TOM4'): {"keep": False, "code": 'dtm' },			# Tom4[srcch14]

    # Microphone groups for Tom3.
    ('Tom3', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbL[srcch0] + AmbR[srcch1]
    # ('Tom3', 'DTM'): {"keep": False, "code": 'dtm' },			# Trigger[srcch15]
    # ('Tom3', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch2]
    # ('Tom3', 'KDRUML'): {"keep": False, "code": 'dim' },			# KdrumL[srcch3]
    # ('Tom3', 'KDRUMR'): {"keep": False, "code": 'dim' },			# KdrumR[srcch4]
    ('Tom3', 'OH'): {"keep": True, "code": 'ons' },			# OHL[srcch5] + OHR[srcch6]
    # ('Tom3', 'RIDEL'): {"keep": False, "code": 'dtm' },			# RideL[srcch7]
    # ('Tom3', 'RIDER'): {"keep": False, "code": 'dtm' },			# RideR[srcch8]
    # ('Tom3', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# Snare_bottom[srcch9]
    # ('Tom3', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# Snare_top[srcch10]
    # ('Tom3', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch11]
    # ('Tom3', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch12]
    ('Tom3', 'TOM3'): {"keep": True, "code": 'dtm' },			# Tom3[srcch13]
    # ('Tom3', 'TOM4'): {"keep": False, "code": 'dtm' },			# Tom4[srcch14]

    # Microphone groups for Tom4.
    ('Tom4', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbL[srcch0] + AmbR[srcch1]
    # ('Tom4', 'DTM'): {"keep": False, "code": 'dtm' },			# Trigger[srcch15]
    # ('Tom4', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch2]
    # ('Tom4', 'KDRUML'): {"keep": False, "code": 'dim' },			# KdrumL[srcch3]
    # ('Tom4', 'KDRUMR'): {"keep": False, "code": 'dim' },			# KdrumR[srcch4]
    ('Tom4', 'OH'): {"keep": True, "code": 'ons' },			# OHL[srcch5] + OHR[srcch6]
    # ('Tom4', 'RIDEL'): {"keep": False, "code": 'dtm' },			# RideL[srcch7]
    # ('Tom4', 'RIDER'): {"keep": False, "code": 'dtm' },			# RideR[srcch8]
    # ('Tom4', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# Snare_bottom[srcch9]
    # ('Tom4', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# Snare_top[srcch10]
    # ('Tom4', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch11]
    # ('Tom4', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch12]
    # ('Tom4', 'TOM3'): {"keep": False, "code": 'dtm' },			# Tom3[srcch13]
    ('Tom4', 'TOM4'): {"keep": True, "code": 'dtm' },			# Tom4[srcch14]

}

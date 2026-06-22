# Auto-generated review map for CrocellKit.
# Edit values here, then run the corresponding convert_*.py script.

INSTRUMENTS = {
    'ChinaL': {
        "instrument": 'china-L1',
        "articulation": 'edge',
        "parameter": '',
    },
    'ChinaR': {
        "instrument": 'china-R1',
        "articulation": 'edge',
        "parameter": '',
    },
    'CrashL': {
        "instrument": 'crash-L1',
        "articulation": 'edge',
        "parameter": '',
    },
    'CrashLStopped': {
        "instrument": 'crash-L1',
        "articulation": 'choke',
        "parameter": '',
    },
    'CrashR': {
        "instrument": 'crash-R1',
        "articulation": 'edge',
        "parameter": '',
    },
    'CrashRStopped': {
        "instrument": 'crash-R1',
        "articulation": 'choke',
        "parameter": '',
    },
    'CrashRXtra': {
        "instrument": 'crash-R2',
        "articulation": 'edge',
        "parameter": '',
    },
    'FTom1': {
        "instrument": 'tom-X3',
        "articulation": 'center',
        "parameter": '',
    },
    'FTom2': {
        "instrument": 'tom-X4',
        "articulation": 'center',
        "parameter": '',
    },
    'HihatClosed': {
        "instrument": 'hihat-X1',
        "articulation": 'bow[tight]',
        "parameter": '-pdl_opn_cls085',
    },
    'HihatClosedNoPedal': {
        "instrument": 'hihat-X1',
        "articulation": 'edge[close]',
        "parameter": '-pdl_opn_cls078',
    },
    'HihatOpen': {
        "instrument": 'hihat-X1',
        "articulation": 'edge[open]',
        "parameter": '-pdl_opn_cls043',
    },
    'HihatPedal': {
        "instrument": 'hihat-X1',
        "articulation": 'pedal',
        "parameter": '-pdl_opn_cls085',
    },
    'HihatPedalHit': {
        "instrument": 'hihat-X1',
        "articulation": 'pedal[splash]',
        "parameter": '-pdl_opn_cls085',
    },
    'HihatSemiOpen': {
        "instrument": 'hihat-X1',
        "articulation": 'edge[half]',
        "parameter": '-pdl_opn_cls052',
    },
    'KDrumL': {
        "instrument": 'kick-L1',
        "articulation": 'center',
        "parameter": '',
    },
    'KDrumR': {
        "instrument": 'kick-R1',
        "articulation": 'center',
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
    'SnareRim': {
        "instrument": 'snare-X1',
        "articulation": 'rim',
        "parameter": '',
    },
    'SnareRimShot': {
        "instrument": 'snare-X1',
        "articulation": 'rimshot',
        "parameter": '',
    },
    'SplashL': {
        "instrument": 'splash-L1',
        "articulation": 'edge',
        "parameter": '',
    },
    'SplashR': {
        "instrument": 'splash-R1',
        "articulation": 'edge',
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
}

MICS = {
    # Microphone groups for ChinaL.
    ('ChinaL', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft[srcch0] + AmbRight[srcch1]
    # ('ChinaL', 'FTOM1'): {"keep": False, "code": 'dtm' },			# FTom1[srcch2]
    # ('ChinaL', 'FTOM2'): {"keep": False, "code": 'dtm' },			# FTom2[srcch3]
    # ('ChinaL', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch4]
    # ('ChinaL', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside[srcch5]
    # ('ChinaL', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside[srcch6]
    ('ChinaL', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft[srcch8] + OHRight[srcch9]
    ('ChinaL', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter[srcch7]
    # ('ChinaL', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride[srcch10]
    # ('ChinaL', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom[srcch11]
    # ('ChinaL', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop[srcch12]
    # ('ChinaL', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch13]
    # ('ChinaL', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch14]

    # Microphone groups for ChinaR.
    ('ChinaR', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft[srcch0] + AmbRight[srcch1]
    # ('ChinaR', 'FTOM1'): {"keep": False, "code": 'dtm' },			# FTom1[srcch2]
    # ('ChinaR', 'FTOM2'): {"keep": False, "code": 'dtm' },			# FTom2[srcch3]
    # ('ChinaR', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch4]
    # ('ChinaR', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside[srcch5]
    # ('ChinaR', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside[srcch6]
    ('ChinaR', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft[srcch8] + OHRight[srcch9]
    ('ChinaR', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter[srcch7]
    # ('ChinaR', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride[srcch10]
    # ('ChinaR', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom[srcch11]
    # ('ChinaR', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop[srcch12]
    # ('ChinaR', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch13]
    # ('ChinaR', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch14]

    # Microphone groups for CrashL.
    ('CrashL', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft[srcch0] + AmbRight[srcch1]
    # ('CrashL', 'FTOM1'): {"keep": False, "code": 'dtm' },			# FTom1[srcch2]
    # ('CrashL', 'FTOM2'): {"keep": False, "code": 'dtm' },			# FTom2[srcch3]
    # ('CrashL', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch4]
    # ('CrashL', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside[srcch5]
    # ('CrashL', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside[srcch6]
    ('CrashL', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft[srcch8] + OHRight[srcch9]
    ('CrashL', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter[srcch7]
    # ('CrashL', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride[srcch10]
    # ('CrashL', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom[srcch11]
    # ('CrashL', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop[srcch12]
    # ('CrashL', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch13]
    # ('CrashL', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch14]

    # Microphone groups for CrashLStopped.
    ('CrashLStopped', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft[srcch0] + AmbRight[srcch1]
    # ('CrashLStopped', 'FTOM1'): {"keep": False, "code": 'dtm' },			# FTom1[srcch2]
    # ('CrashLStopped', 'FTOM2'): {"keep": False, "code": 'dtm' },			# FTom2[srcch3]
    # ('CrashLStopped', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch4]
    # ('CrashLStopped', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside[srcch5]
    # ('CrashLStopped', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside[srcch6]
    ('CrashLStopped', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft[srcch8] + OHRight[srcch9]
    ('CrashLStopped', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter[srcch7]
    # ('CrashLStopped', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride[srcch10]
    # ('CrashLStopped', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom[srcch11]
    # ('CrashLStopped', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop[srcch12]
    # ('CrashLStopped', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch13]
    # ('CrashLStopped', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch14]

    # Microphone groups for CrashR.
    ('CrashR', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft[srcch0] + AmbRight[srcch1]
    # ('CrashR', 'FTOM1'): {"keep": False, "code": 'dtm' },			# FTom1[srcch2]
    # ('CrashR', 'FTOM2'): {"keep": False, "code": 'dtm' },			# FTom2[srcch3]
    # ('CrashR', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch4]
    # ('CrashR', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside[srcch5]
    # ('CrashR', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside[srcch6]
    ('CrashR', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft[srcch8] + OHRight[srcch9]
    ('CrashR', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter[srcch7]
    # ('CrashR', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride[srcch10]
    # ('CrashR', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom[srcch11]
    # ('CrashR', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop[srcch12]
    # ('CrashR', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch13]
    # ('CrashR', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch14]

    # Microphone groups for CrashRStopped.
    ('CrashRStopped', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft[srcch0] + AmbRight[srcch1]
    # ('CrashRStopped', 'FTOM1'): {"keep": False, "code": 'dtm' },			# FTom1[srcch2]
    # ('CrashRStopped', 'FTOM2'): {"keep": False, "code": 'dtm' },			# FTom2[srcch3]
    # ('CrashRStopped', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch4]
    # ('CrashRStopped', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside[srcch5]
    # ('CrashRStopped', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside[srcch6]
    ('CrashRStopped', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft[srcch8] + OHRight[srcch9]
    ('CrashRStopped', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter[srcch7]
    # ('CrashRStopped', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride[srcch10]
    # ('CrashRStopped', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom[srcch11]
    # ('CrashRStopped', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop[srcch12]
    # ('CrashRStopped', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch13]
    # ('CrashRStopped', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch14]

    # Microphone groups for CrashRXtra.
    ('CrashRXtra', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft[srcch0] + AmbRight[srcch1]
    # ('CrashRXtra', 'FTOM1'): {"keep": False, "code": 'dtm' },			# FTom1[srcch2]
    # ('CrashRXtra', 'FTOM2'): {"keep": False, "code": 'dtm' },			# FTom2[srcch3]
    # ('CrashRXtra', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch4]
    # ('CrashRXtra', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside[srcch5]
    # ('CrashRXtra', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside[srcch6]
    ('CrashRXtra', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft[srcch8] + OHRight[srcch9]
    ('CrashRXtra', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter[srcch7]
    # ('CrashRXtra', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride[srcch10]
    # ('CrashRXtra', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom[srcch11]
    # ('CrashRXtra', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop[srcch12]
    # ('CrashRXtra', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch13]
    # ('CrashRXtra', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch14]

    # Microphone groups for FTom1.
    ('FTom1', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft[srcch0] + AmbRight[srcch1]
    ('FTom1', 'FTOM1'): {"keep": True, "code": 'dtm' },			# FTom1[srcch2]
    # ('FTom1', 'FTOM2'): {"keep": False, "code": 'dtm' },			# FTom2[srcch3]
    # ('FTom1', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch4]
    # ('FTom1', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside[srcch5]
    # ('FTom1', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside[srcch6]
    ('FTom1', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft[srcch8] + OHRight[srcch9]
    ('FTom1', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter[srcch7]
    # ('FTom1', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride[srcch10]
    # ('FTom1', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom[srcch11]
    # ('FTom1', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop[srcch12]
    # ('FTom1', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch13]
    # ('FTom1', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch14]

    # Microphone groups for FTom2.
    ('FTom2', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft[srcch0] + AmbRight[srcch1]
    # ('FTom2', 'FTOM1'): {"keep": False, "code": 'dtm' },			# FTom1[srcch2]
    ('FTom2', 'FTOM2'): {"keep": True, "code": 'dtm' },			# FTom2[srcch3]
    # ('FTom2', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch4]
    # ('FTom2', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside[srcch5]
    # ('FTom2', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside[srcch6]
    ('FTom2', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft[srcch8] + OHRight[srcch9]
    ('FTom2', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter[srcch7]
    # ('FTom2', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride[srcch10]
    # ('FTom2', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom[srcch11]
    # ('FTom2', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop[srcch12]
    # ('FTom2', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch13]
    # ('FTom2', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch14]

    # Microphone groups for HihatClosed.
    ('HihatClosed', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft[srcch0] + AmbRight[srcch1]
    # ('HihatClosed', 'FTOM1'): {"keep": False, "code": 'dtm' },			# FTom1[srcch2]
    # ('HihatClosed', 'FTOM2'): {"keep": False, "code": 'dtm' },			# FTom2[srcch3]
    ('HihatClosed', 'HIHAT'): {"keep": True, "code": 'dtm' },			# Hihat[srcch4]
    # ('HihatClosed', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside[srcch5]
    # ('HihatClosed', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside[srcch6]
    ('HihatClosed', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft[srcch8] + OHRight[srcch9]
    ('HihatClosed', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter[srcch7]
    # ('HihatClosed', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride[srcch10]
    # ('HihatClosed', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom[srcch11]
    # ('HihatClosed', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop[srcch12]
    # ('HihatClosed', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch13]
    # ('HihatClosed', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch14]

    # Microphone groups for HihatClosedNoPedal.
    ('HihatClosedNoPedal', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft[srcch0] + AmbRight[srcch1]
    # ('HihatClosedNoPedal', 'FTOM1'): {"keep": False, "code": 'dtm' },			# FTom1[srcch2]
    # ('HihatClosedNoPedal', 'FTOM2'): {"keep": False, "code": 'dtm' },			# FTom2[srcch3]
    ('HihatClosedNoPedal', 'HIHAT'): {"keep": True, "code": 'dtm' },			# Hihat[srcch4]
    # ('HihatClosedNoPedal', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside[srcch5]
    # ('HihatClosedNoPedal', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside[srcch6]
    ('HihatClosedNoPedal', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft[srcch8] + OHRight[srcch9]
    ('HihatClosedNoPedal', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter[srcch7]
    # ('HihatClosedNoPedal', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride[srcch10]
    # ('HihatClosedNoPedal', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom[srcch11]
    # ('HihatClosedNoPedal', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop[srcch12]
    # ('HihatClosedNoPedal', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch13]
    # ('HihatClosedNoPedal', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch14]

    # Microphone groups for HihatOpen.
    ('HihatOpen', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft[srcch0] + AmbRight[srcch1]
    # ('HihatOpen', 'FTOM1'): {"keep": False, "code": 'dtm' },			# FTom1[srcch2]
    # ('HihatOpen', 'FTOM2'): {"keep": False, "code": 'dtm' },			# FTom2[srcch3]
    ('HihatOpen', 'HIHAT'): {"keep": True, "code": 'dtm' },			# Hihat[srcch4]
    # ('HihatOpen', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside[srcch5]
    # ('HihatOpen', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside[srcch6]
    ('HihatOpen', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft[srcch8] + OHRight[srcch9]
    ('HihatOpen', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter[srcch7]
    # ('HihatOpen', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride[srcch10]
    # ('HihatOpen', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom[srcch11]
    # ('HihatOpen', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop[srcch12]
    # ('HihatOpen', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch13]
    # ('HihatOpen', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch14]

    # Microphone groups for HihatPedal.
    ('HihatPedal', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft[srcch0] + AmbRight[srcch1]
    # ('HihatPedal', 'FTOM1'): {"keep": False, "code": 'dtm' },			# FTom1[srcch2]
    # ('HihatPedal', 'FTOM2'): {"keep": False, "code": 'dtm' },			# FTom2[srcch3]
    ('HihatPedal', 'HIHAT'): {"keep": True, "code": 'dtm' },			# Hihat[srcch4]
    # ('HihatPedal', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside[srcch5]
    # ('HihatPedal', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside[srcch6]
    ('HihatPedal', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft[srcch8] + OHRight[srcch9]
    ('HihatPedal', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter[srcch7]
    # ('HihatPedal', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride[srcch10]
    # ('HihatPedal', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom[srcch11]
    # ('HihatPedal', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop[srcch12]
    # ('HihatPedal', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch13]
    # ('HihatPedal', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch14]

    # Microphone groups for HihatPedalHit.
    ('HihatPedalHit', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft[srcch0] + AmbRight[srcch1]
    # ('HihatPedalHit', 'FTOM1'): {"keep": False, "code": 'dtm' },			# FTom1[srcch2]
    # ('HihatPedalHit', 'FTOM2'): {"keep": False, "code": 'dtm' },			# FTom2[srcch3]
    ('HihatPedalHit', 'HIHAT'): {"keep": True, "code": 'dtm' },			# Hihat[srcch4]
    # ('HihatPedalHit', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside[srcch5]
    # ('HihatPedalHit', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside[srcch6]
    ('HihatPedalHit', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft[srcch8] + OHRight[srcch9]
    ('HihatPedalHit', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter[srcch7]
    # ('HihatPedalHit', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride[srcch10]
    # ('HihatPedalHit', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom[srcch11]
    # ('HihatPedalHit', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop[srcch12]
    # ('HihatPedalHit', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch13]
    # ('HihatPedalHit', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch14]

    # Microphone groups for HihatSemiOpen.
    ('HihatSemiOpen', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft[srcch0] + AmbRight[srcch1]
    # ('HihatSemiOpen', 'FTOM1'): {"keep": False, "code": 'dtm' },			# FTom1[srcch2]
    # ('HihatSemiOpen', 'FTOM2'): {"keep": False, "code": 'dtm' },			# FTom2[srcch3]
    ('HihatSemiOpen', 'HIHAT'): {"keep": True, "code": 'dtm' },			# Hihat[srcch4]
    # ('HihatSemiOpen', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside[srcch5]
    # ('HihatSemiOpen', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside[srcch6]
    ('HihatSemiOpen', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft[srcch8] + OHRight[srcch9]
    ('HihatSemiOpen', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter[srcch7]
    # ('HihatSemiOpen', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride[srcch10]
    # ('HihatSemiOpen', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom[srcch11]
    # ('HihatSemiOpen', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop[srcch12]
    # ('HihatSemiOpen', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch13]
    # ('HihatSemiOpen', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch14]

    # Microphone groups for KDrumL.
    ('KDrumL', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft[srcch0] + AmbRight[srcch1]
    # ('KDrumL', 'FTOM1'): {"keep": False, "code": 'dtm' },			# FTom1[srcch2]
    # ('KDrumL', 'FTOM2'): {"keep": False, "code": 'dtm' },			# FTom2[srcch3]
    # ('KDrumL', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch4]
    ('KDrumL', 'KICK_IN'): {"keep": True, "code": 'dim' },			# KDrumInside[srcch5]
    ('KDrumL', 'KICK_OUT'): {"keep": True, "code": 'dom' },			# KDrumOutside[srcch6]
    ('KDrumL', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft[srcch8] + OHRight[srcch9]
    ('KDrumL', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter[srcch7]
    # ('KDrumL', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride[srcch10]
    # ('KDrumL', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom[srcch11]
    # ('KDrumL', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop[srcch12]
    # ('KDrumL', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch13]
    # ('KDrumL', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch14]

    # Microphone groups for KDrumR.
    ('KDrumR', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft[srcch0] + AmbRight[srcch1]
    # ('KDrumR', 'FTOM1'): {"keep": False, "code": 'dtm' },			# FTom1[srcch2]
    # ('KDrumR', 'FTOM2'): {"keep": False, "code": 'dtm' },			# FTom2[srcch3]
    # ('KDrumR', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch4]
    ('KDrumR', 'KICK_IN'): {"keep": True, "code": 'dim' },			# KDrumInside[srcch5]
    ('KDrumR', 'KICK_OUT'): {"keep": True, "code": 'dom' },			# KDrumOutside[srcch6]
    ('KDrumR', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft[srcch8] + OHRight[srcch9]
    ('KDrumR', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter[srcch7]
    # ('KDrumR', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride[srcch10]
    # ('KDrumR', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom[srcch11]
    # ('KDrumR', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop[srcch12]
    # ('KDrumR', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch13]
    # ('KDrumR', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch14]

    # Microphone groups for RideR.
    ('RideR', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft[srcch0] + AmbRight[srcch1]
    # ('RideR', 'FTOM1'): {"keep": False, "code": 'dtm' },			# FTom1[srcch2]
    # ('RideR', 'FTOM2'): {"keep": False, "code": 'dtm' },			# FTom2[srcch3]
    # ('RideR', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch4]
    # ('RideR', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside[srcch5]
    # ('RideR', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside[srcch6]
    ('RideR', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft[srcch8] + OHRight[srcch9]
    ('RideR', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter[srcch7]
    ('RideR', 'RIDE'): {"keep": True, "code": 'dtm' },			# Ride[srcch10]
    # ('RideR', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom[srcch11]
    # ('RideR', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop[srcch12]
    # ('RideR', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch13]
    # ('RideR', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch14]

    # Microphone groups for RideRBell.
    ('RideRBell', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft[srcch0] + AmbRight[srcch1]
    # ('RideRBell', 'FTOM1'): {"keep": False, "code": 'dtm' },			# FTom1[srcch2]
    # ('RideRBell', 'FTOM2'): {"keep": False, "code": 'dtm' },			# FTom2[srcch3]
    # ('RideRBell', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch4]
    # ('RideRBell', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside[srcch5]
    # ('RideRBell', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside[srcch6]
    ('RideRBell', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft[srcch8] + OHRight[srcch9]
    ('RideRBell', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter[srcch7]
    ('RideRBell', 'RIDE'): {"keep": True, "code": 'dtm' },			# Ride[srcch10]
    # ('RideRBell', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom[srcch11]
    # ('RideRBell', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop[srcch12]
    # ('RideRBell', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch13]
    # ('RideRBell', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch14]

    # Microphone groups for Snare.
    ('Snare', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft[srcch0] + AmbRight[srcch1]
    # ('Snare', 'FTOM1'): {"keep": False, "code": 'dtm' },			# FTom1[srcch2]
    # ('Snare', 'FTOM2'): {"keep": False, "code": 'dtm' },			# FTom2[srcch3]
    # ('Snare', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch4]
    # ('Snare', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside[srcch5]
    # ('Snare', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside[srcch6]
    ('Snare', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft[srcch8] + OHRight[srcch9]
    ('Snare', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter[srcch7]
    # ('Snare', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride[srcch10]
    ('Snare', 'SNARE_BOTTOM'): {"keep": True, "code": 'dbm' },			# SnareBottom[srcch11]
    ('Snare', 'SNARE_TOP'): {"keep": True, "code": 'dtm' },			# SnareTop[srcch12]
    # ('Snare', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch13]
    # ('Snare', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch14]

    # Microphone groups for SnareRest.
    ('SnareRest', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft[srcch0] + AmbRight[srcch1]
    # ('SnareRest', 'FTOM1'): {"keep": False, "code": 'dtm' },			# FTom1[srcch2]
    # ('SnareRest', 'FTOM2'): {"keep": False, "code": 'dtm' },			# FTom2[srcch3]
    # ('SnareRest', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch4]
    # ('SnareRest', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside[srcch5]
    # ('SnareRest', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside[srcch6]
    ('SnareRest', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft[srcch8] + OHRight[srcch9]
    ('SnareRest', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter[srcch7]
    # ('SnareRest', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride[srcch10]
    ('SnareRest', 'SNARE_BOTTOM'): {"keep": True, "code": 'dbm' },			# SnareBottom[srcch11]
    ('SnareRest', 'SNARE_TOP'): {"keep": True, "code": 'dtm' },			# SnareTop[srcch12]
    # ('SnareRest', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch13]
    # ('SnareRest', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch14]

    # Microphone groups for SnareRim.
    ('SnareRim', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft[srcch0] + AmbRight[srcch1]
    # ('SnareRim', 'FTOM1'): {"keep": False, "code": 'dtm' },			# FTom1[srcch2]
    # ('SnareRim', 'FTOM2'): {"keep": False, "code": 'dtm' },			# FTom2[srcch3]
    # ('SnareRim', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch4]
    # ('SnareRim', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside[srcch5]
    # ('SnareRim', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside[srcch6]
    ('SnareRim', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft[srcch8] + OHRight[srcch9]
    ('SnareRim', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter[srcch7]
    # ('SnareRim', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride[srcch10]
    ('SnareRim', 'SNARE_BOTTOM'): {"keep": True, "code": 'dbm' },			# SnareBottom[srcch11]
    ('SnareRim', 'SNARE_TOP'): {"keep": True, "code": 'dtm' },			# SnareTop[srcch12]
    # ('SnareRim', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch13]
    # ('SnareRim', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch14]

    # Microphone groups for SnareRimShot.
    ('SnareRimShot', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft[srcch0] + AmbRight[srcch1]
    # ('SnareRimShot', 'FTOM1'): {"keep": False, "code": 'dtm' },			# FTom1[srcch2]
    # ('SnareRimShot', 'FTOM2'): {"keep": False, "code": 'dtm' },			# FTom2[srcch3]
    # ('SnareRimShot', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch4]
    # ('SnareRimShot', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside[srcch5]
    # ('SnareRimShot', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside[srcch6]
    ('SnareRimShot', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft[srcch8] + OHRight[srcch9]
    ('SnareRimShot', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter[srcch7]
    # ('SnareRimShot', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride[srcch10]
    ('SnareRimShot', 'SNARE_BOTTOM'): {"keep": True, "code": 'dbm' },			# SnareBottom[srcch11]
    ('SnareRimShot', 'SNARE_TOP'): {"keep": True, "code": 'dtm' },			# SnareTop[srcch12]
    # ('SnareRimShot', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch13]
    # ('SnareRimShot', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch14]

    # Microphone groups for SplashL.
    ('SplashL', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft[srcch0] + AmbRight[srcch1]
    # ('SplashL', 'FTOM1'): {"keep": False, "code": 'dtm' },			# FTom1[srcch2]
    # ('SplashL', 'FTOM2'): {"keep": False, "code": 'dtm' },			# FTom2[srcch3]
    # ('SplashL', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch4]
    # ('SplashL', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside[srcch5]
    # ('SplashL', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside[srcch6]
    ('SplashL', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft[srcch8] + OHRight[srcch9]
    ('SplashL', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter[srcch7]
    # ('SplashL', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride[srcch10]
    # ('SplashL', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom[srcch11]
    # ('SplashL', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop[srcch12]
    # ('SplashL', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch13]
    # ('SplashL', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch14]

    # Microphone groups for SplashR.
    ('SplashR', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft[srcch0] + AmbRight[srcch1]
    # ('SplashR', 'FTOM1'): {"keep": False, "code": 'dtm' },			# FTom1[srcch2]
    # ('SplashR', 'FTOM2'): {"keep": False, "code": 'dtm' },			# FTom2[srcch3]
    # ('SplashR', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch4]
    # ('SplashR', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside[srcch5]
    # ('SplashR', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside[srcch6]
    ('SplashR', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft[srcch8] + OHRight[srcch9]
    ('SplashR', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter[srcch7]
    # ('SplashR', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride[srcch10]
    # ('SplashR', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom[srcch11]
    # ('SplashR', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop[srcch12]
    # ('SplashR', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch13]
    # ('SplashR', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch14]

    # Microphone groups for Tom1.
    ('Tom1', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft[srcch0] + AmbRight[srcch1]
    # ('Tom1', 'FTOM1'): {"keep": False, "code": 'dtm' },			# FTom1[srcch2]
    # ('Tom1', 'FTOM2'): {"keep": False, "code": 'dtm' },			# FTom2[srcch3]
    # ('Tom1', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch4]
    # ('Tom1', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside[srcch5]
    # ('Tom1', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside[srcch6]
    ('Tom1', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft[srcch8] + OHRight[srcch9]
    ('Tom1', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter[srcch7]
    # ('Tom1', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride[srcch10]
    # ('Tom1', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom[srcch11]
    # ('Tom1', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop[srcch12]
    ('Tom1', 'TOM1'): {"keep": True, "code": 'dtm' },			# Tom1[srcch13]
    # ('Tom1', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2[srcch14]

    # Microphone groups for Tom2.
    ('Tom2', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft[srcch0] + AmbRight[srcch1]
    # ('Tom2', 'FTOM1'): {"keep": False, "code": 'dtm' },			# FTom1[srcch2]
    # ('Tom2', 'FTOM2'): {"keep": False, "code": 'dtm' },			# FTom2[srcch3]
    # ('Tom2', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat[srcch4]
    # ('Tom2', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside[srcch5]
    # ('Tom2', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside[srcch6]
    ('Tom2', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft[srcch8] + OHRight[srcch9]
    ('Tom2', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter[srcch7]
    # ('Tom2', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride[srcch10]
    # ('Tom2', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom[srcch11]
    # ('Tom2', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop[srcch12]
    # ('Tom2', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1[srcch13]
    ('Tom2', 'TOM2'): {"keep": True, "code": 'dtm' },			# Tom2[srcch14]

}

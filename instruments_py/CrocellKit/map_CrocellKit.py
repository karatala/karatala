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
        "articulation": 'bow[close]',
        "parameter": '-pdl_opn_cls078',
    },
    'HihatClosedNoPedal': {
        "instrument": 'hihat-X1',
        "articulation": 'edge[half]',
        "parameter": '-pdl_opn_cls064',
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
        "instrument": 'snare-X2',
        "articulation": 'rest',
        "parameter": '',
    },
    'SnareRim': {
        "instrument": 'snare-X3',
        "articulation": 'rim',
        "parameter": '',
    },
    'SnareRimShot': {
        "instrument": 'snare-X4',
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
    ('ChinaL', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft + AmbRight
    # ('ChinaL', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat
    # ('ChinaL', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside
    # ('ChinaL', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside
    ('ChinaL', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft + OHRight
    ('ChinaL', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter
    # ('ChinaL', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride
    # ('ChinaL', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom
    # ('ChinaL', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop
    # ('ChinaL', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1
    # ('ChinaL', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2

    # Microphone groups for ChinaR.
    ('ChinaR', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft + AmbRight
    # ('ChinaR', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat
    # ('ChinaR', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside
    # ('ChinaR', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside
    ('ChinaR', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft + OHRight
    ('ChinaR', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter
    # ('ChinaR', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride
    # ('ChinaR', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom
    # ('ChinaR', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop
    # ('ChinaR', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1
    # ('ChinaR', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2

    # Microphone groups for CrashL.
    ('CrashL', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft + AmbRight
    # ('CrashL', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat
    # ('CrashL', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside
    # ('CrashL', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside
    ('CrashL', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft + OHRight
    ('CrashL', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter
    # ('CrashL', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride
    # ('CrashL', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom
    # ('CrashL', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop
    # ('CrashL', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1
    # ('CrashL', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2

    # Microphone groups for CrashLStopped.
    ('CrashLStopped', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft + AmbRight
    # ('CrashLStopped', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat
    # ('CrashLStopped', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside
    # ('CrashLStopped', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside
    ('CrashLStopped', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft + OHRight
    ('CrashLStopped', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter
    # ('CrashLStopped', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride
    # ('CrashLStopped', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom
    # ('CrashLStopped', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop
    # ('CrashLStopped', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1
    # ('CrashLStopped', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2

    # Microphone groups for CrashR.
    ('CrashR', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft + AmbRight
    # ('CrashR', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat
    # ('CrashR', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside
    # ('CrashR', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside
    ('CrashR', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft + OHRight
    ('CrashR', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter
    # ('CrashR', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride
    # ('CrashR', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom
    # ('CrashR', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop
    # ('CrashR', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1
    # ('CrashR', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2

    # Microphone groups for CrashRStopped.
    ('CrashRStopped', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft + AmbRight
    # ('CrashRStopped', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat
    # ('CrashRStopped', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside
    # ('CrashRStopped', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside
    ('CrashRStopped', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft + OHRight
    ('CrashRStopped', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter
    # ('CrashRStopped', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride
    # ('CrashRStopped', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom
    # ('CrashRStopped', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop
    # ('CrashRStopped', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1
    # ('CrashRStopped', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2

    # Microphone groups for CrashRXtra.
    ('CrashRXtra', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft + AmbRight
    # ('CrashRXtra', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat
    # ('CrashRXtra', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside
    # ('CrashRXtra', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside
    ('CrashRXtra', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft + OHRight
    ('CrashRXtra', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter
    # ('CrashRXtra', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride
    # ('CrashRXtra', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom
    # ('CrashRXtra', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop
    # ('CrashRXtra', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1
    # ('CrashRXtra', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2

    # Microphone groups for FTom1.
    ('FTom1', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft + AmbRight
    # ('FTom1', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat
    # ('FTom1', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside
    # ('FTom1', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside
    ('FTom1', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft + OHRight
    ('FTom1', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter
    # ('FTom1', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride
    # ('FTom1', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom
    # ('FTom1', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop
    ('FTom1', 'TOM1'): {"keep": True, "code": 'dtm' },			# Tom1
    # ('FTom1', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2

    # Microphone groups for FTom2.
    ('FTom2', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft + AmbRight
    # ('FTom2', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat
    # ('FTom2', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside
    # ('FTom2', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside
    ('FTom2', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft + OHRight
    ('FTom2', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter
    # ('FTom2', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride
    # ('FTom2', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom
    # ('FTom2', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop
    # ('FTom2', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1
    ('FTom2', 'TOM2'): {"keep": True, "code": 'dtm' },			# Tom2

    # Microphone groups for HihatClosed.
    ('HihatClosed', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft + AmbRight
    ('HihatClosed', 'HIHAT'): {"keep": True, "code": 'dtm' },			# Hihat
    # ('HihatClosed', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside
    # ('HihatClosed', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside
    ('HihatClosed', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft + OHRight
    ('HihatClosed', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter
    # ('HihatClosed', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride
    # ('HihatClosed', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom
    # ('HihatClosed', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop
    # ('HihatClosed', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1
    # ('HihatClosed', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2

    # Microphone groups for HihatClosedNoPedal.
    ('HihatClosedNoPedal', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft + AmbRight
    ('HihatClosedNoPedal', 'HIHAT'): {"keep": True, "code": 'dtm' },			# Hihat
    # ('HihatClosedNoPedal', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside
    # ('HihatClosedNoPedal', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside
    ('HihatClosedNoPedal', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft + OHRight
    ('HihatClosedNoPedal', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter
    # ('HihatClosedNoPedal', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride
    # ('HihatClosedNoPedal', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom
    # ('HihatClosedNoPedal', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop
    # ('HihatClosedNoPedal', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1
    # ('HihatClosedNoPedal', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2

    # Microphone groups for HihatOpen.
    ('HihatOpen', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft + AmbRight
    ('HihatOpen', 'HIHAT'): {"keep": True, "code": 'dtm' },			# Hihat
    # ('HihatOpen', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside
    # ('HihatOpen', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside
    ('HihatOpen', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft + OHRight
    ('HihatOpen', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter
    # ('HihatOpen', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride
    # ('HihatOpen', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom
    # ('HihatOpen', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop
    # ('HihatOpen', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1
    # ('HihatOpen', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2

    # Microphone groups for HihatPedal.
    ('HihatPedal', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft + AmbRight
    ('HihatPedal', 'HIHAT'): {"keep": True, "code": 'dtm' },			# Hihat
    # ('HihatPedal', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside
    # ('HihatPedal', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside
    ('HihatPedal', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft + OHRight
    ('HihatPedal', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter
    # ('HihatPedal', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride
    # ('HihatPedal', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom
    # ('HihatPedal', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop
    # ('HihatPedal', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1
    # ('HihatPedal', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2

    # Microphone groups for HihatPedalHit.
    ('HihatPedalHit', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft + AmbRight
    ('HihatPedalHit', 'HIHAT'): {"keep": True, "code": 'dtm' },			# Hihat
    # ('HihatPedalHit', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside
    # ('HihatPedalHit', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside
    ('HihatPedalHit', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft + OHRight
    ('HihatPedalHit', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter
    # ('HihatPedalHit', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride
    # ('HihatPedalHit', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom
    # ('HihatPedalHit', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop
    # ('HihatPedalHit', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1
    # ('HihatPedalHit', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2

    # Microphone groups for HihatSemiOpen.
    ('HihatSemiOpen', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft + AmbRight
    ('HihatSemiOpen', 'HIHAT'): {"keep": True, "code": 'dtm' },			# Hihat
    # ('HihatSemiOpen', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside
    # ('HihatSemiOpen', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside
    ('HihatSemiOpen', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft + OHRight
    ('HihatSemiOpen', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter
    # ('HihatSemiOpen', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride
    # ('HihatSemiOpen', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom
    # ('HihatSemiOpen', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop
    # ('HihatSemiOpen', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1
    # ('HihatSemiOpen', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2

    # Microphone groups for KDrumL.
    ('KDrumL', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft + AmbRight
    # ('KDrumL', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat
    # ('KDrumL', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside
    # ('KDrumL', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside
    ('KDrumL', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft + OHRight
    ('KDrumL', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter
    # ('KDrumL', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride
    # ('KDrumL', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom
    # ('KDrumL', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop
    # ('KDrumL', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1
    # ('KDrumL', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2

    # Microphone groups for KDrumR.
    ('KDrumR', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft + AmbRight
    # ('KDrumR', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat
    # ('KDrumR', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside
    # ('KDrumR', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside
    ('KDrumR', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft + OHRight
    ('KDrumR', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter
    # ('KDrumR', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride
    # ('KDrumR', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom
    # ('KDrumR', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop
    # ('KDrumR', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1
    # ('KDrumR', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2

    # Microphone groups for RideR.
    ('RideR', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft + AmbRight
    # ('RideR', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat
    # ('RideR', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside
    # ('RideR', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside
    ('RideR', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft + OHRight
    ('RideR', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter
    ('RideR', 'RIDE'): {"keep": True, "code": 'dtm' },			# Ride
    # ('RideR', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom
    # ('RideR', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop
    # ('RideR', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1
    # ('RideR', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2

    # Microphone groups for RideRBell.
    ('RideRBell', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft + AmbRight
    # ('RideRBell', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat
    # ('RideRBell', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside
    # ('RideRBell', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside
    ('RideRBell', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft + OHRight
    ('RideRBell', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter
    ('RideRBell', 'RIDE'): {"keep": True, "code": 'dtm' },			# Ride
    # ('RideRBell', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom
    # ('RideRBell', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop
    # ('RideRBell', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1
    # ('RideRBell', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2

    # Microphone groups for Snare.
    ('Snare', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft + AmbRight
    # ('Snare', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat
    # ('Snare', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside
    # ('Snare', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside
    ('Snare', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft + OHRight
    ('Snare', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter
    # ('Snare', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride
    ('Snare', 'SNARE_BOTTOM'): {"keep": True, "code": 'dbm' },			# SnareBottom
    ('Snare', 'SNARE_TOP'): {"keep": True, "code": 'dtm' },			# SnareTop
    # ('Snare', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1
    # ('Snare', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2

    # Microphone groups for SnareRest.
    ('SnareRest', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft + AmbRight
    # ('SnareRest', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat
    # ('SnareRest', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside
    # ('SnareRest', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside
    ('SnareRest', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft + OHRight
    ('SnareRest', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter
    # ('SnareRest', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride
    ('SnareRest', 'SNARE_BOTTOM'): {"keep": True, "code": 'dbm' },			# SnareBottom
    ('SnareRest', 'SNARE_TOP'): {"keep": True, "code": 'dtm' },			# SnareTop
    # ('SnareRest', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1
    # ('SnareRest', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2

    # Microphone groups for SnareRim.
    ('SnareRim', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft + AmbRight
    # ('SnareRim', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat
    # ('SnareRim', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside
    # ('SnareRim', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside
    ('SnareRim', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft + OHRight
    ('SnareRim', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter
    # ('SnareRim', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride
    ('SnareRim', 'SNARE_BOTTOM'): {"keep": True, "code": 'dbm' },			# SnareBottom
    ('SnareRim', 'SNARE_TOP'): {"keep": True, "code": 'dtm' },			# SnareTop
    # ('SnareRim', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1
    # ('SnareRim', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2

    # Microphone groups for SnareRimShot.
    ('SnareRimShot', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft + AmbRight
    # ('SnareRimShot', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat
    # ('SnareRimShot', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside
    # ('SnareRimShot', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside
    ('SnareRimShot', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft + OHRight
    ('SnareRimShot', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter
    # ('SnareRimShot', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride
    ('SnareRimShot', 'SNARE_BOTTOM'): {"keep": True, "code": 'dbm' },			# SnareBottom
    ('SnareRimShot', 'SNARE_TOP'): {"keep": True, "code": 'dtm' },			# SnareTop
    # ('SnareRimShot', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1
    # ('SnareRimShot', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2

    # Microphone groups for SplashL.
    ('SplashL', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft + AmbRight
    # ('SplashL', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat
    # ('SplashL', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside
    # ('SplashL', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside
    ('SplashL', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft + OHRight
    ('SplashL', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter
    # ('SplashL', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride
    # ('SplashL', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom
    # ('SplashL', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop
    # ('SplashL', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1
    # ('SplashL', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2

    # Microphone groups for SplashR.
    ('SplashR', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft + AmbRight
    # ('SplashR', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat
    # ('SplashR', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside
    # ('SplashR', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside
    ('SplashR', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft + OHRight
    ('SplashR', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter
    # ('SplashR', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride
    # ('SplashR', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom
    # ('SplashR', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop
    # ('SplashR', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1
    # ('SplashR', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2

    # Microphone groups for Tom1.
    ('Tom1', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft + AmbRight
    # ('Tom1', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat
    # ('Tom1', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside
    # ('Tom1', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside
    ('Tom1', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft + OHRight
    ('Tom1', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter
    # ('Tom1', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride
    # ('Tom1', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom
    # ('Tom1', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop
    ('Tom1', 'TOM1'): {"keep": True, "code": 'dtm' },			# Tom1
    # ('Tom1', 'TOM2'): {"keep": False, "code": 'dtm' },			# Tom2

    # Microphone groups for Tom2.
    ('Tom2', 'AMB'): {"keep": True, "code": 'rfs' },			# AmbLeft + AmbRight
    # ('Tom2', 'HIHAT'): {"keep": False, "code": 'dtm' },			# Hihat
    # ('Tom2', 'KICK_IN'): {"keep": False, "code": 'dim' },			# KDrumInside
    # ('Tom2', 'KICK_OUT'): {"keep": False, "code": 'dom' },			# KDrumOutside
    ('Tom2', 'OH'): {"keep": True, "code": 'ons' },			# OHLeft + OHRight
    ('Tom2', 'OH_CENTER'): {"keep": True, "code": 'rcm' },			# OHCenter
    # ('Tom2', 'RIDE'): {"keep": False, "code": 'dtm' },			# Ride
    # ('Tom2', 'SNARE_BOTTOM'): {"keep": False, "code": 'dbm' },			# SnareBottom
    # ('Tom2', 'SNARE_TOP'): {"keep": False, "code": 'dtm' },			# SnareTop
    # ('Tom2', 'TOM1'): {"keep": False, "code": 'dtm' },			# Tom1
    ('Tom2', 'TOM2'): {"keep": True, "code": 'dtm' },			# Tom2

}

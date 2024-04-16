# Changelog
## [Alpha v0.2.0]
### Fixed
- Edges near / far were flipped.
- Max bar height `p_accel_max_height` prevent bug when bar overgrow out of screen at speeds more then 7000ups.

### Updated
- Bars can be vertically centered now `p_accel 0bXxxxxxxxxxxxxxx`.
- Edges functionality extended by size and centering `p_accel_edge 0bXXx`.
- Submodule cgame_proxymod updated to latest, most notably support for defrag 1.91.31, see more at (changelog)[https://github.com/Jelvan1/cgame_proxymod/blob/cfa74ea141f7dfe36b1398869d163a30dbbb8fa0/CHANGELOG.md]

### Added
- Added offset for vertical centering `p_accel_vcenter_offset`.
- Added base bar height `p_accel_base_height` aka minimal bar height.
- Added bar height clipping cvar `p_accel_max_height`.
- Added aim zone feature which grow from far window bar zone, the `p_accel_aim_zone*` cvars family.
- Added cvars for edges height control `p_accel_edge_height`, `p_accel_edge_min_height`.


## [Alpha v0.1.1]
### Added
- Added new feature of drawing center of window bar with different color `p_accel_window_center`, window mode doesn't have to be enabled for this to work.

## [Alpha v0.1.0]
### Added
- Added new cursor hud `p_cursor`, which draws lines or custom image at center, to aim better at accel zones.
- Added support for "main window" mode `p_accel 0bxxxxXxxxxxxxxx`, both for regular graph and predictions, with custom colors and highlighting, options for window grow by threshold (`p_accel_window_grow_limit`, `p_accel_window_threshold`).
- Added colored edges of positive graph `p_accel_edge`, similar to cgaz min/max zone.
- Added option for flipping growth of negative bars `p_accel 0bxXxxxxxxxxxxxx`.
- Added color altering in regular graph (experimental) `p_accel 0bXxxxxxxxxxxxxx`.
- Added option for bar mergin by threshold `p_accel_merge_threshold`.

### Changed
- Moves and prediction `p_accel_p_*` now accepts binary value instead, backward compatible with old values, in addition you can set window mode for individual move / prediction.

## [Alpha v0.0.10]
### Updated
- Submodule cgame_proxymod updated to latest, most notably support for defrag 1.91.29 and 1.91.30, see more at (changelog)[https://github.com/Jelvan1/cgame_proxymod/blob/9c1f95c348aed9dc914d2d0cd35a9ec385648b06/CHANGELOG.md]

## [Alpha v0.0.9]
### Added
- Added new config option `p_accel_threshold` to be able to limit the minimal acceleration in graph.

## [Alpha v0.0.8]
### Added
- Added new prediction of jump/crunch `p_accel_p_cj`, special offset for that prediction `p_accel_p_cj_offset`.
- Added highlighting greater adjacent zone (experimental) `p_accel 0bXxxxxxxxx`


## [Alpha v0.0.7]
### Added
- New cvars `p_accel_draw_order` etc. per each hud, to alter their draw ordering.

### Fixed
- Accel hud now uses the `mdd_projection` configuration.

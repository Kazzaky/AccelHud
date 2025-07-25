# AccelHud extension to mDd client Proxymod

[![build](https://github.com/Kazzaky/AccelHud/actions/workflows/build_all.yml/badge.svg)](https://github.com/Kazzaky/AccelHud/actions/workflows/build_all.yml)

![demo of accel hud](./demo-small.gif)

Please note that the AccelHud is currently only alpha version, might be buggy.

## Binaries:

|            | **Windows** | **Linux** | **macOS** |
| :--------: | :---------: | :-------: | :-------: |
| **32-bit** | [cgamex86.dll](../../releases/download/v0.3.2/cgamex86.dll) | [cgamei386.so](../../releases/download/v0.3.2/cgamei386.so) |
| **64-bit** | [cgamex86_64.dll](../../releases/download/v0.3.2/cgamex86_64.dll) | [cgamex86_64.so](../../releases/download/v0.3.2/cgamex86_64.so) | [cgamex86_64.dylib](../../releases/download/v0.3.2/cgamex86_64.dylib) |

## Build / Instalation

Follow instructions at [Jelvan1/cgame_proxymod](https://github.com/Jelvan1/cgame_proxymod) which is the original mod, this repository is only extention for it.

In addition for macOS build, you need *binutils* package, use `brew install binutils`, then provide path to the **objcopy** program installed with binutils to cmake as argument `-DOBJCOPY_CMD=path/to/the/objcopy` (unless you add the binutils installation folder to environment variable PATH).

Only gcc and clang compilers are supported.

If you wish you can replace cgame_proxymod submodule to [Jelvan1/cgame_proxymod](https://github.com/Jelvan1/cgame_proxymod) in order to obtain most actual mDd client.

## Notes

In terms of this hud "main window" refer to the most near to velocity vector bar, nothing magic about that, since there are usually few micro bars merge threshold and window threshold help to reach what is generally considered main window.

## Configuration options

`p_accel 0bXXXXXXXXXXXXXX` - main settings of accel hud\
Xxxxxxxxxxxxxxx - center bars vertically\
xXxxxxxxxxxxxxx - color alternate (experimental)\
xxXxxxxxxxxxxxx - flip negative bars, they grow up instead down\
xxxXxxxxxxxxxxx - highlight main window\
xxxxXxxxxxxxxxx - custom color for main window\
xxxxxXxxxxxxxxx - draw only main window\
xxxxxxXxxxxxxxx - highlight greater adjecent zone (experimental)\
xxxxxxxXxxxxxxx - uniform acceleration value\
xxxxxxxxXxxxxxx - draw condensed acceleration line\
xxxxxxxxxXxxxxx - draw current acceleration line\
xxxxxxxxxxXxxxx - draw vertical lines\
xxxxxxxxxxxXxxx - disable drawing bars\
xxxxxxxxxxxxXxx - draw line graph\
xxxxxxxxxxxxxXx - highlight active zone\
xxxxxxxxxxxxxxX - draw basic hud

`p_accel_trueness 0bXXXX`\
Xxxx - disable dynamic acceleration value correction\
xXxx - show true ground control zones\
xxXx - show true CPM air control zones\
xxxX - show true jump/crouch zones

`p_accel_vline 0bXX`\
Xx - use custom color for vertical lines\
xX - include line graph height for vertical lines height

`p_accel_min_speed X` - minimal speed in ups, which is required to draw hud

`p_accel_neg_mode X` - modes for showing negative acceleration

0 - disable negative acceleration\
1 - enable negative acceleration\
2 - show only adjecent negative acceleration

`p_accel_edge 0bXXXXXX` - draw edges of positive graph portion in regular graph, similar like cgaz min/max zone, except edges do not grow and are not really part of graph, if you reach them then you are already out of the positive range

0 - disable\
Xxxxxx - forced vertical center\
xXxxxx - use custom forced vertical center ^\
xxXxxx - makes `p_accel_edge_height` relative (percentage)\
xxxXxx - makes `p_accel_edge_size` relative (percentage)\
xxxxXx - extend height to negative graph\
xxxxxX - draw basic edges

`p_accel_merge_threshold X` - maximal width of bars which will merge to longer, value is in pixels, 0 will disable merging

`p_accel_window_threshold X` - maximal width of bars which will be showed together with main window, only relevant in window mode, affects both regular graph and prediction, only value greater then `p_accel_merge_threshold` make sense, otherwise they will get merged and never reached by window threshold

`p_accel_window_grow_limit X` - limits how many bars in total can be showed together with main window, each have to fullfil the `p_accel_window_threshold`, only relevant in window mode, affects both regular graph and prediction, when 0 only 

`p_accel_threshold X` - minimal accel value to show individual graph bar, while in air the range is from 0 to 1.5, you could use for example 0.2 to hide plasma climb hud flickering, please note that this option will **negatively affect** normal hud function by adding delay before showing or hiding prematurely the hud bars, when there is relevant information to show (even tho "insignificant")

`p_accel_window_center 0bXXXXXX` - will add zone to window bar in the center to easily navigate between rotation rate (kinda superseded with aim zone).\
Xxxxxx - forced vertical center\
xXxxxx - use custom forced vertical center ^\
xxXxxx - makes `p_accel_window_center_height` relative (percentage)\
xxxXxx - makes `p_accel_window_center_size` relative (percentage)\
xxxxXx - enable zone highlight\
xxxxxX - drawing of basic window center

`p_accel_aim_zone 0bXXXXXXX` - will add zone to window bar at the far end where the rotation is higher (thus reaching the higher accel faster), similary like window center this is useful to easily navigate between rotation rate.\
Xxxxxxx - forced vertical center\
xXxxxxx - use custom forced vertical center ^\
xxXxxxx - subtract the aim zone from window zone (otherwise overdrawn)\
xxxXxxx - makes `p_accel_aim_zone_height` relative (percentage)\
xxxxXxx - makes `p_accel_aim_zone_size` relative (percentage)\
xxxxxXx - enable zone highlight\
xxxxxxX - draw basic aim zone

### Proportions:
`p_accel_yh X X`\
X x - y coord of hud center\
x X - height of hud (approximate)

#### Sizes:
`p_accel_line_size X` - size of line in line graph mode\
`p_accel_vline_size X` - size of vertical lines\
`p_accel_point_line_size X` - size of current acceleration line\
`p_accel_edge_size X` - size of edges\
`p_accel_edge_min_size X` - minimal size of edges\
`p_accel_cond_size X` - size of condensed acceleration line\
`p_accel_window_center_size X` - size of window bar center (could be percentage)\
`p_accel_window_center_min_size X` - minimal size of window bar center\
`p_accel_aim_zone_size` - size of the aim zone (could be percentage)\
`p_accel_aim_zone_min_size` - minimal size of the aim zone

#### Offsets:
`p_accel_p_offset X` - offset of predictions\
`p_accel_p_cj_offset X` - offset of crouch/jump prediction\
`p_accel_vcenter_offset X` - offset added to vertically centered bars (away from condensed line)\
`p_accel_edge_voffset X` - vertical offset added to edges\
`p_accel_window_center_voffset X` - vertical offsed added to window center\
`p_accel_aim_zone_voffset X` - vertical offset added to aim zone

#### Heights:
`p_accel_base_height X` - aka minimal height for each bar, but "base" as its added not clipped\
`p_accel_max_height X` - maximal bar height (clip off)\
`p_accel_edge_height X` - custom height of the edges or -1 for same height as graph bar\
`p_accel_edge_min_height X` - minimal height of edges\
`p_accel_window_center_height X` - custom height of window center or -1 for same height as graph bar\
`p_accel_window_center_min_height X` - minimal height of window center\
`p_accel_aim_zone_height X` - cutom height of the aim zone or -1 for the same height as graph bar\
`p_accel_aim_zone_min_height X` - minimal height of aim zone


Each value is relative to 640x480 resolution, scaled up to real resolution.

### Moves:

`p_accel_show_move 0bXXXX` - indicate which move will draw regular accel graph, this is good for disabling moves without useful information like sidemove, rest of the hud like predictions still works.\
Xxxx - side while grounded (slick) (A or D)\
xXxx - forward (W or S)\
xxXx - side (A or D)\
xxxX - strafe (WA, WD, SA, SD)

`p_accel_show_move_vq3 0bXXXX` - vq3 specific version of `p_accel_show_move`.


### Predictions:

There are three types of predictions: **strafe**, **sidemove** *(vq3 only)* and **crouch/jump**. Each is activated by specific move.

The cvar name is constructed as follows:\
`p_accel` - prefix\
`_p` - "predict"\
`_strafe` - prediction type (like which move to draw in addition)\
*[`strafe` - strafe, `sm` - sidemove, `cj` - crouch/jump]*\
`_w` - "while" holding key combination (move)\
`_sm` - the move to activate the prediction\
*[`sm` - sidemove, `fm` - forwardmove, `nk` - no key, `strafe` - strafe]*\
`_vq3` - specific for vq3 physics (*optional - cpm is without suffix*)


#### Strafe:
`p_accel_p_strafe_w_sm 0bXX`\
`p_accel_p_strafe_w_fm 0bXX`\
`p_accel_p_strafe_w_nk 0bXX`\
`p_accel_p_strafe_w_strafe 0bXX`

`p_accel_p_strafe_w_sm_vq3 0bXX`\
`p_accel_p_strafe_w_fm_vq3 0bXX`\
`p_accel_p_strafe_w_nk_vq3 0bXX`\
`p_accel_p_strafe_w_strafe_vq3 0bXX`

#### Sidemove:
`p_accel_p_sm_w_sm_vq3 0bXX`\
`p_accel_p_sm_w_strafe_vq3 0bXX`\
`p_accel_p_sm_w_fm_vq3 0bXX`\
`p_accel_p_sm_w_nk_vq3 0bXX`

#### Crouch/jump:
`p_accel_p_cj_w_strafe 0bXX`

`p_accel_p_cj_w_strafe_vq3 0bXX`\
`p_accel_p_cj_w_sm_vq3 0bXX`

Values for all predictions:\
0 - do not predict\
Xx - only main window\
xX - draw prediction

#### Special:
`p_accel_p_cj_overdraw X` - draw jump/crouch prediction on top of regular move\
0 - no\
1 - yes

### Colors:
`p_accel_rgba X X X X` - color of positive acceleration\
`p_accel_alt_rgba X X X X` - alternation color for positive acceleration\
`p_accel_neg_rgba X X X X` - color of negative acceleration\
`p_accel_hl_rgba X X X X` - highlight color of positive acceleration\
`p_accel_hl_g_adj_rgba X X X X` - highlight color of greater adjecent zone\
`p_accel_hl_neg_rgba X X X X` - highlight color of negative acceleration

`p_accel_cur_rgba X X X X` - color of line for current acceleration

`p_accel_line_rgba X X X X` - color of positive line when in line graph mode\
`p_accel_line_alt_rgba X X X X` - alternating color for line graph mode\
`p_accel_line_neg_rgba X X X X` - color of negative line when in line graph mode\
`p_accel_line_hl_rgba X X X X` - highlight color of positive line when in line graph mode\
`p_accel_line_hl_g_adj_rgba X X X X` - highlight color of greater adjecent zone line\
`p_accel_line_hl_neg_rgba X X X X` - highlight color of negative line when in line graph mode

`p_accel_vline_rgba X X X X` - custom color for vertical lines

`p_accel_zero_rgba X X X X` - color of zero acceleration in condensed acceleration line

`p_accel_window_rgba X X X X` - custom color for main window\
`p_accel_window_hl_rgba X X X X` - hightlight color for main window\
`p_accel_line_window_rgba X X X X` - color for main window in line graph mode\
`p_accel_line_window_hl_rgba X X X X` - highlight color for main window in line graph mode

`p_accel_near_edge_rgba X X X X` - near edge color (minimal cgaz like)\
`p_accel_far_edge_rgba X X X X` - far edge color (maximal cgaz like)

`p_accel_window_center_rgba X X X X` - color of window bar center\
`p_accel_window_center_hl_rgba X X X X` - highlight color of window bar center\
`p_accel_line_window_center_rgba X X X X` - color of window bar center in line graph mode\
`p_accel_line_window_center_hl_rgba X X X X` - highlight color of window bar center in line graph mode

`p_accel_aim_zone_rgba` - color of aim zone\
`p_accel_line_aim_zone_rgba` - color of aim zone in line graph mode\
`p_accel_aim_zone_hl_rgba` - highlight color of aim zone\
`p_accel_line_aim_zone_hl_rgba` - highlight color of aim zone in line graph mode

Colors order is: Red Green Blue Alpha, each as value between 0 and 1.\
For example: `p_accel_rgba .2 .9 .2 .6`.

### Color modificators:
`p_accel_p_strafe_rgbam X X X X` - color modificator for predicting strafe\
`p_accel_p_sm_rgbam X X X X` - color modificator for predicting sidemove\
`p_accel_p_opposite_rgbam X X X X` - color modificator for predicting opposite side\
`p_accel_p_cj_rgbam X X X X` - color modificatior for predicting same move with jump/crouch


Values can range from -1 to 1, for example `p_accel_p_strafe_rgbam -.2 -.1 .2 -.2` would make of
`p_accel_rgba .2 .9 .2 .6` new color: `0 .8 .4 .4`.

## Cursor:

`p_cursor 0bXXXX`\
Xxxx - use custom image\
xXxx - draw top and bottom line\
xxXx - double the vertical size, y coord point to middle\
xxxX - draw basic cursor

`p_cursor_custom_tga X` - tga image or shader resource filepath (without file extension), which is looked up in all pk3 and filesystem

### Cursor proportions:
`p_cursor_line_size X` - size of the lines\
`p_cursor_yhw X X X` - y coords, height and width\
`p_cursor_offset X X` - x y coords offset

Each value is relative to 640x480 resolution, scaled up to real resolution.

### Cursor colors:
`p_cursor_rgba X X X X` - cursor color

Colors order is: Red Green Blue Alpha, each as value between 0 and 1.\
For example: `p_cursor_rgba .1 .7 1 .8`.

## Huds draw order:

Each cvar use value between 0-8, in case same value is set the default order is used.

`p_compass_draw_order X`\
`p_cgaz_draw_order X`\
`p_snap_draw_order X`\
`p_pitch_draw_order X`\
`p_ammo_draw_order X`\
`p_jump_draw_order X`\
`p_timer_draw_order X`\
`p_accel_draw_order X`\
`p_cursor_draw_order X`

For example if you want to draw snap hud on top of accel hud, just make sure the `p_snap_draw_order` have greater value then `p_accel_draw_order`.

## Special:
`p_flickfree X` - fixes flickering issue of cgaz, snap and accel at first frame after user input changes.


## Configs:
In repository root is folder `config-example` with configs you can try out. Copy them into your `/defrag` folder, then run following command in your game console: `exec filename`, you can add this command into your autoexec.cfg to make the config load permanent.
\
\
\
That's it, I hope someone finds this useful.

// Open Pocket GBC handheld case (OpenSCAD). Vertical, Pocket/DMG-like.
// Front shell: 2.4" ILI9341 window, D-pad, A/B, Select/Start/Menu, speaker grille.
// Back shell: battery bay, Tang Nano 20K bay with HDMI (left) and USB-C (right) openings,
// charging USB-C and 3.5 mm jack at the bottom, link port at the top, 4 screw bosses.
//
//   openscad -o front.stl -D part=\"front\" open_pocket_case.scad
//   openscad -o back.stl  -D part=\"back\"  open_pocket_case.scad
//   openscad -o preview.png -D part=\"assembly\" --render open_pocket_case.scad
//
// All positions are in mm from the lower left corner of the carrier PCB (hardware/pcb),
// which sits inside the case with 1.6 mm of wall around it.

part = "assembly";        // front | back | assembly | buttons

$fn = 48;

// ---------------------------------------------------------------- main dimensions
pcb_w = 86;  pcb_h = 136;  pcb_t = 1.6;
wall = 2.0;  gap = 0.4;
case_w = pcb_w + 2 * (wall + gap);
case_h = pcb_h + 2 * (wall + gap);
corner = 6;               // corner radius
corner_br = 22;           // the big Game Boy radius, bottom right
front_d = 7.5;            // front shell depth (PCB top face to front surface)
back_d = 15;              // back shell depth (PCB bottom face to back surface): Tang + battery
screw = [[6, 6], [pcb_w - 6, 6], [6, pcb_h - 6], [pcb_w - 6, pcb_h - 6]];

// ---------------------------------------------------------------- parts on the carrier PCB
// 2.4" ILI9341 module, landscape: PCB 77.1 x 42.7, glass active area 48.96 x 36.72 offset
lcd_c = [pcb_w / 2, 104];                 // centre of the active area
lcd_view = [50, 38];                      // window in the front (slightly larger than active)
lcd_bezel = [66, 52];                     // the dark bezel recess
dpad_c = [22, 46];  dpad_arm = [9.5, 27];
a_c = [68, 52];  b_c = [56, 42];  ab_d = 11;
small_c = [[24, 20], [37, 20], [50, 20]];  // Select, Start, Menu: pill buttons
small_sz = [9, 4.5];
spk_c = [73, 15];                         // speaker grille
usbc_x = 40;  jack_x = 18;                // bottom edge
link_x = 60;                              // top edge
tang_y = 104;                             // Tang Nano 20K, horizontal behind the screen
hdmi_z = 4;  usb_tang_z = 3.5;            // heights below the PCB of the side connectors

module rounded_box(w, h, d, r, rbr) {
    hull() {
        translate([r, r, 0]) cylinder(r = r, h = d);
        translate([w - rbr, rbr, 0]) cylinder(r = rbr, h = d);
        translate([r, h - r, 0]) cylinder(r = r, h = d);
        translate([w - r, h - r, 0]) cylinder(r = r, h = d);
    }
}

module pill(l, w, d) {
    hull() { translate([-l / 2 + w / 2, 0, 0]) cylinder(d = w, h = d); translate([l / 2 - w / 2, 0, 0]) cylinder(d = w, h = d); }
}

module dpad_shape(d, extra = 0) {
    translate([dpad_c[0], dpad_c[1], 0]) {
        translate([-(dpad_arm[0] + extra) / 2, -(dpad_arm[1] + extra) / 2, 0]) cube([dpad_arm[0] + extra, dpad_arm[1] + extra, d]);
        translate([-(dpad_arm[1] + extra) / 2, -(dpad_arm[0] + extra) / 2, 0]) cube([dpad_arm[1] + extra, dpad_arm[0] + extra, d]);
    }
}

// inner offset: the PCB area in case coordinates
module at_pcb() { translate([wall + gap, wall + gap, 0]) children(); }

module front() {
    difference() {
        rounded_box(case_w, case_h, front_d, corner, corner_br);
        // hollow
        translate([wall, wall, -0.01]) rounded_box(case_w - 2 * wall, case_h - 2 * wall, front_d - wall + 0.01, corner - wall, corner_br - wall);
        at_pcb() {
            // screen: bezel recess and window
            translate([lcd_c[0] - lcd_bezel[0] / 2, lcd_c[1] - lcd_bezel[1] / 2, front_d - 0.8]) cube([lcd_bezel[0], lcd_bezel[1], 1]);
            translate([lcd_c[0] - lcd_view[0] / 2, lcd_c[1] - lcd_view[1] / 2, 0]) cube([lcd_view[0], lcd_view[1], front_d + 1]);
            // buttons
            dpad_shape(front_d + 1, 0.8);
            for (c = [a_c, b_c]) translate([c[0], c[1], 0]) cylinder(d = ab_d + 0.8, h = front_d + 1);
            for (c = small_c) translate([c[0], c[1], 0]) pill(small_sz[0] + 0.8, small_sz[1] + 0.8, front_d + 1);
            // speaker grille: slots at the classic angle
            for (i = [0:5]) translate([spk_c[0] - 6 + i * 3, spk_c[1] - 7, 0]) rotate([0, 0, 0]) hull() {
                cylinder(d = 1.6, h = front_d + 1); translate([0, 12, 0]) cylinder(d = 1.6, h = front_d + 1);
            }
        }
        // engraved name above the screen
        at_pcb() translate([pcb_w / 2, 128.5, front_d - 0.5]) linear_extrude(1)
            text("OPEN POCKET", size = 3.6, halign = "center", valign = "center", font = "Liberation Sans:style=Bold");
    }
    // screw bosses (M2 self-tapping from the back)
    at_pcb() for (s = screw) translate([s[0], s[1], 0]) difference() {
        cylinder(d = 5.5, h = front_d - wall + 0.01);
        translate([0, 0, -0.01]) cylinder(d = 1.8, h = front_d);
    }
    // the screen rests on these posts (module thickness ~4.3 mm with glass)
    at_pcb() for (dx = [-36, 36], dy = [-19, 19]) translate([lcd_c[0] + dx, lcd_c[1] + dy, 0]) cylinder(d = 3, h = front_d - wall - 4.3);
}

module back() {
    difference() {
        rounded_box(case_w, case_h, back_d, corner, corner_br);
        translate([wall, wall, wall]) rounded_box(case_w - 2 * wall, case_h - 2 * wall, back_d, corner - wall, corner_br - wall);
        at_pcb() {
            // bottom edge: charging USB-C and the 3.5 mm jack (PCB face at z = back_d)
            translate([usbc_x - 5, -wall - gap - 1, back_d - 3.5 - 1.6]) cube([10, wall + 2, 3.6]);
            translate([jack_x, wall + 1, back_d - 3.2]) rotate([90, 0, 0]) cylinder(d = 6.4, h = wall + gap + 3);
            // top edge: link port
            translate([link_x - 7, pcb_h - 0.5, back_d - 5.5]) cube([14, wall + gap + 2, 5]);
            // left side: HDMI of the Tang Nano 20K; right side: its USB-C (flashing, remote)
            translate([-wall - gap - 1, tang_y - 8, back_d - 1.6 - hdmi_z - 5.5]) cube([wall + 2, 16, 6.5]);
            translate([pcb_w - 0.5, tang_y - 5, back_d - 1.6 - usb_tang_z - 3.5]) cube([wall + gap + 2, 10, 3.8]);
            // power switch slot on the right side
            translate([pcb_w - 0.5, 60, back_d - 1.6 - 3]) cube([wall + gap + 2, 8, 3]);
            // microSD of the Tang Nano reachable through the left side
            translate([-wall - gap - 1, tang_y + 12, back_d - 1.6 - 9]) cube([wall + 2, 13, 2.5]);
            // screw holes and heads
            for (s = screw) translate([s[0], s[1], -0.01]) { cylinder(d = 2.3, h = back_d); cylinder(d = 4.2, h = 1.6); }
        }
        // a recessed label area
        at_pcb() translate([pcb_w / 2 - 25, 30, -0.01]) cube([50, 30, 0.6]);
    }
    // screw posts up to the PCB
    at_pcb() for (s = screw) translate([s[0], s[1], 0]) difference() {
        cylinder(d = 5.5, h = back_d - pcb_t);
        translate([0, 0, -0.01]) cylinder(d = 2.3, h = back_d);
    }
    // battery bay walls (503450-604060 class LiPo, up to 62 x 42 x 6.5)
    at_pcb() translate([pcb_w / 2 - 33, 8, 0]) difference() {
        cube([66, 46, 8.5]);
        translate([1.2, 1.2, wall]) cube([63.6, 43.6, 10]);
    }
}

module buttons() {
    // D-pad cross, A, B and three pills, printed separately (rubber domes or tact switches below)
    translate([0, 0, 0]) dpad_shape(front_d + 1.2);
    for (c = [a_c, b_c]) translate([c[0], c[1], 0]) cylinder(d = ab_d, h = front_d + 1.2);
    for (c = small_c) translate([c[0], c[1], 0]) pill(small_sz[0], small_sz[1], front_d - 1);
}

if (part == "front") front();
else if (part == "back") back();
else if (part == "buttons") buttons();
else {
    // exploded assembly: back, PCB, front, buttons
    color("#2b2b2b") back();
    color("#1f6f3f") translate([wall + gap, wall + gap, back_d - pcb_t]) cube([pcb_w, pcb_h, pcb_t]);
    color("#111") translate([wall + gap + lcd_c[0] - 38.5, wall + gap + lcd_c[1] - 21.3, back_d + 0.5]) cube([77, 42.6, 4.3]);
    color("#d9d9d4") translate([0, 0, back_d + 14]) front();
    color("#333") translate([wall + gap, wall + gap, back_d + 26]) buttons();
}

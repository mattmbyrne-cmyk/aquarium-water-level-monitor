
//
// STEPPED ELECTRONIC ENCLOSURE
// VERSION: 10mm RECESS + 2 HORIZONTAL PCB POSTS
//
// Units: mm
//
// OUTER DIMENSIONS: 114 x 104 x 25
//
// FRONT:
// - Upper recessed section: 40 mm
// - Recess depth: 10 mm
// - Full-width vertical step face
// - Six LED holes: 5.2 mm
//
// REAR:
// - Four lid screw posts
// - Removable inset lid
// - Left rectangular cut-out
// - Right semicircular cut-out
//
// PCB:
// - Two additional mounting posts
// - Posts extend horizontally from inside front face
// - Posts point towards rear lid
// - Post length: 11 mm
//

$fn = 80;


// ==========================================
// 1. PCB DIMENSIONS
// ==========================================

pcb_width = 90;
pcb_height = 82;

clearance = 1;
wall = 2;

outer_width = 114;
outer_height = 95;

pcb_offset_x = (outer_width-pcb_width)/2;
pcb_offset_z = (outer_height-pcb_height)/2;


// ==========================================
// 2. ENCLOSURE DIMENSIONS
// ==========================================

box_depth = 25;

step_depth = 11;
upper_height = 40;

front_thickness = 2;

// Step position: 40 mm from top

step_from_top = 40;
step_z = outer_height-step_from_top;


// ==========================================
// 3. LED DIMENSIONS
// ==========================================

led_diameter = 5.2;
led_count = 6;

first_led_from_left = 7.3;
first_led_from_top = 15;

led_total_spacing = 73;
led_spacing = led_total_spacing/(led_count-1);

// LED positions relative to PCB

first_led_x = pcb_offset_x+first_led_from_left;

first_led_z = pcb_offset_z+pcb_height-first_led_from_top;


// ==========================================
// 4. REAR LID SCREW POSTS
// ==========================================

post_diameter = 9;
post_length = 11;

pilot_diameter = 2.5;

post_left_x = 6;
post_right_x = outer_width-6;

post_bottom_z = 5.5;
post_top_z = outer_height-5.5;

post_rear_y = box_depth-2;


// ==========================================
// 5. PCB MOUNTING POSTS
// ==========================================

pcb_post_diameter = 6;
pcb_post_height = 12;
pcb_post_hole_diameter = 2.5;

// Right PCB mounting post

//pcb_post_1_x = outer_width-15;
pcb_post_1_x = outer_width-17;
pcb_post_1_z = 10.5;

// Left PCB mounting post

pcb_post_2_x = 22;
pcb_post_2_z = 10.5;

// Posts start at inside front face
// and extend towards rear lid.

pcb_post_start_y = front_thickness;


// ==========================================
// 6. REAR LID DIMENSIONS
// ==========================================

lid_thickness = 2;
lid_clearance = 0.2;

inner_width = outer_width-2*wall;
inner_height = outer_height-2*wall;

lid_width = inner_width-2*lid_clearance;
lid_height = inner_height-2*lid_clearance;

lid_x = wall+lid_clearance;
lid_z = wall+lid_clearance;

lid_y = box_depth-lid_thickness;

lid_hole_diameter = 3.2;


// ==========================================
// 7. LEFT RECTANGULAR REAR CUT-OUT
// ==========================================

left_notch_start_z = 22.5;
left_notch_height = 5.8;
left_notch_depth = 3;


// ==========================================
// 8. RIGHT SEMICIRCULAR REAR CUT-OUT
// ==========================================

right_notch_diameter = 5;

right_notch_reference_z = 31;
right_notch_top_offset = 4;

right_notch_top_z = 
    right_notch_reference_z-right_notch_top_offset;

right_notch_center_z = 
    right_notch_top_z-right_notch_diameter;
    

// ==========================================
// 9. RIGHT SIDE ROUND HOLE
// ==========================================

side_hole_diameter = 12;

side_hole_bottom_z = 35 ;

side_hole_center_z = 
     side_hole_bottom_z+side_hole_diameter/2;

//side_hole_center_y = post_rear_y-10;
side_hole_center_y = 9;



// ==========================================
// 10. DISPLAY OPTIONS
// ==========================================

show_lid = true;


// ==========================================
// 11. OUTER STEPPED SHELL
// ==========================================

module outer_shell() {

    union() {

        // LOWER SECTION
        // Full front-to-back depth

        cube([
            outer_width,
            box_depth,
            step_z
        ]);

        // UPPER SECTION
        // Recessed exactly 10 mm

        translate([
            0,
            step_depth,
            step_z
        ])
            cube([
                outer_width,
                box_depth-step_depth,
                upper_height
            ]);
    }
}


// ==========================================
// 12. FULL-WIDTH VERTICAL STEP FACE
// ==========================================

module vertical_step_face() {

    translate([
        0,
        0,
        step_z
    ])
        cube([
            outer_width,
            step_depth,
            front_thickness
        ]);
}


// ==========================================
// 13. COMPLETE OUTER BODY
// ==========================================

module complete_outer_body() {

    union() {

        outer_shell();

        vertical_step_face();
    }
}


// ==========================================
// 14. INTERNAL CAVITY
// ==========================================

module internal_cavity() {

    union() {

        // LOWER INTERNAL CAVITY

        translate([
            wall,
            wall,
            wall
        ])
            cube([
                inner_width,
                box_depth+1,
                step_z-2*wall
            ]);

        // UPPER INTERNAL CAVITY

        translate([
            wall,
            step_depth+wall,
            step_z+wall
        ])
            cube([
                inner_width,
                box_depth+1,
                upper_height-2*wall
            ]);

        // CONNECTION BEHIND STEP

        translate([
            wall,
            step_depth+wall,
            step_z-wall
        ])
            cube([
                inner_width,
                box_depth+1,
                2*wall
            ]);
    }
}


// ==========================================
// 15. SIX LED HOLES
// ==========================================

module led_holes() {

    for (i = [0:led_count-1]) {

        led_x = first_led_x+i*led_spacing;

        translate([
            led_x,
            step_depth+front_thickness+0.1,
            first_led_z
        ])
            rotate([90,0,0])
                cylinder(
                    h = front_thickness+0.2,
                    d = led_diameter
                );
    }
}


// ==========================================
// 16. FOUR REAR LID SCREW POSTS
// ==========================================

module screw_posts() {

    for (x = [
        post_left_x,
        post_right_x
    ]) {

        for (z = [
            post_bottom_z,
            post_top_z
        ]) {

            translate([
                x,
                post_rear_y,
                z
            ])
                rotate([90,0,0])
                    cylinder(
                        h = post_length,
                        d = post_diameter
                    );
        }
    }
}


// ==========================================
// 17. REAR LID SCREW PILOT HOLES
// ==========================================

module post_pilot_holes() {

    for (x = [
        post_left_x,
        post_right_x
    ]) {

        for (z = [
            post_bottom_z,
            post_top_z
        ]) {

            translate([
                x,
                post_rear_y+0.2,
                z
            ])
                rotate([90,0,0])
                    cylinder(
                        h = post_length+0.5,
                        d = pilot_diameter
                    );
        }
    }
}


// ==========================================
// 18. TWO HORIZONTAL PCB MOUNTING POSTS
// ==========================================

module pcb_mounting_posts() {

    for (p = [
        [pcb_post_1_x, pcb_post_1_z],
        [pcb_post_2_x, pcb_post_2_z]
    ]) {

        // Posts start at inside front face.
        // They extend in the positive Y direction
        // towards the rear lid.

        translate([
            p[0],
            pcb_post_start_y,
            p[1]
        ])
            rotate([-90,0,0])
                cylinder(
                    h = pcb_post_height,
                    d = pcb_post_diameter
                );
    }
}


// ==========================================
// 19. PCB MOUNTING POST SCREW HOLES
// ==========================================

module pcb_mounting_holes() {

    for (p = [
        [pcb_post_1_x, pcb_post_1_z],
        [pcb_post_2_x, pcb_post_2_z]
    ]) {

        // Holes run along the same direction
        // as the horizontal PCB mounting posts.

        translate([
            p[0],
            pcb_post_start_y-0.1,
            p[1]
        ])
            rotate([-90,0,0])
                cylinder(
                    h = pcb_post_height+0.2,
                    d = pcb_post_hole_diameter
                );
    }
}


// ==========================================
// 20. LEFT RECTANGULAR CUT-OUT
// ==========================================

module left_rectangular_cutout() {

    translate([
        -0.1,
        box_depth-left_notch_depth,
        left_notch_start_z
    ])
        cube([
            wall+0.2,
            left_notch_depth+0.2,
            left_notch_height
        ]);
}


// ==========================================
// 21. RIGHT SEMICIRCULAR CUT-OUT
// ==========================================

module right_semicircular_cutout() {

    translate([
        outer_width-wall-0.1,
        23,
        right_notch_center_z
    ])
        rotate([0,90,0])
            cylinder(
                h = wall+0.2,
                //h = wall+0.2,
                d = right_notch_diameter
            );
}


// ==========================================
// 22. RIGHT SIDE ROUND HOLE
// ==========================================

module right_side_round_hole() {

    translate([
        outer_width-wall-0.1,
        side_hole_center_y,
        side_hole_center_z
    ])
        rotate([0,90,0])
            cylinder(
                h = wall+0.2,
                d = side_hole_diameter
            );
}


// ==========================================
// 23. ALL REAR AND SIDE CUT-OUTS
// ==========================================

module enclosure_cutouts() {

    union() {

        left_rectangular_cutout();

        right_semicircular_cutout();

        right_side_round_hole();
    }
}


// ==========================================
// 24. MAIN ENCLOSURE
// ==========================================

module enclosure() {

    difference() {

        union() {

            // Main shell with internal cavity

            difference() {

                complete_outer_body();

                internal_cavity();

                led_holes();
            }

            // Four rear lid screw posts

            screw_posts();

            // Two horizontal PCB mounting posts

            pcb_mounting_posts();
        }

        // Rear lid screw holes

        post_pilot_holes();

        // PCB mounting screw holes

        pcb_mounting_holes();

        // Rear and side cut-outs

        enclosure_cutouts();
    }
}


// ==========================================
// 25. REAR LID
// ==========================================

module rear_lid() {

    difference() {

        // Lid fits inside enclosure

        translate([
            lid_x,
            lid_y,
            lid_z
        ])
            cube([
                lid_width,
                lid_thickness,
                lid_height
            ]);

        // Four matching screw holes

        for (x = [
            post_left_x,
            post_right_x
        ]) {

            for (z = [
                post_bottom_z,
                post_top_z
            ]) {

                translate([
                    x,
                    lid_y+lid_thickness+0.1,
                    z
                ])
                    rotate([90,0,0])
                        cylinder(
                            h = lid_thickness+0.2,
                            d = lid_hole_diameter
                        );
            }
        }
    }
}


// ==========================================
// 26. DISPLAY
// ==========================================

// Main enclosure

enclosure();

// Separate rear lid

if (show_lid) {

    translate([
        0,
        box_depth+5,
        0
    ])
        rear_lid();
}


// ==========================================
// 27. DIMENSION REPORT
// ==========================================

echo("========== ENCLOSURE ==========");

echo("WIDTH:", outer_width);
echo("HEIGHT:", outer_height);
echo("DEPTH:", box_depth);

echo("STEP FROM TOP:", step_from_top);
echo("STEP DEPTH:", step_depth);
echo("UPPER HEIGHT:", upper_height);

echo("========== PCB ==========");

echo("PCB WIDTH:", pcb_width);
echo("PCB HEIGHT:", pcb_height);

echo("PCB OFFSET X:", pcb_offset_x);
echo("PCB OFFSET Z:", pcb_offset_z);

echo("========== LEDS ==========");

echo("LED COUNT:", led_count);
echo("LED DIAMETER:", led_diameter);
echo("LED SPACING:", led_spacing);

echo("FIRST LED X:", first_led_x);
echo("LAST LED X:", first_led_x+5*led_spacing);
echo("LED Z:", first_led_z);

echo("========== REAR SCREW POSTS ==========");

echo("POST DIAMETER:", post_diameter);
echo("POST LENGTH:", post_length);
echo("PILOT DIAMETER:", pilot_diameter);

echo("========== PCB MOUNTING POSTS ==========");

echo("PCB POST DIAMETER:", pcb_post_diameter);
echo("PCB POST LENGTH:", pcb_post_height);
echo("PCB POST HOLE:", pcb_post_hole_diameter);

echo("RIGHT PCB POST X:", pcb_post_1_x);
echo("RIGHT PCB POST Z:", pcb_post_1_z);

echo("LEFT PCB POST X:", pcb_post_2_x);
echo("LEFT PCB POST Z:", pcb_post_2_z);

echo("PCB POST START Y:", pcb_post_start_y);
echo("PCB POST END Y:", pcb_post_start_y+pcb_post_height);

echo("========== CUTOUTS ==========");

echo("LEFT NOTCH START:", left_notch_start_z);
echo("LEFT NOTCH HEIGHT:", left_notch_height);
echo("LEFT NOTCH DEPTH:", left_notch_depth);

echo("RIGHT NOTCH DIAMETER:", right_notch_diameter);
echo("RIGHT NOTCH TOP:", right_notch_top_z);

echo("SIDE HOLE DIAMETER:", side_hole_diameter);
echo("SIDE HOLE BOTTOM:", side_hole_bottom_z);

echo("========== REAR LID ==========");

echo("LID WIDTH:", lid_width);
echo("LID HEIGHT:", lid_height);
echo("LID THICKNESS:", lid_thickness);

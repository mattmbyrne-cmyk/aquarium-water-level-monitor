
//
// A02YYUW Aquarium Sensor Bracket
// Version 5 - Corrected Glass Clamp
// Units: mm
//

$fn = 80;

// ======================================
// 1. GLASS DIMENSIONS
// ======================================

glass_thickness = 5;
glass_tolerance = 0.2;
glass_slot = glass_thickness + glass_tolerance;

// ======================================
// 2. PLATFORM
// ======================================

platform_length = 98;
platform_width = 35;
platform_thickness = 5;

// ======================================
// 3. GLASS CLAMP
// ======================================

hook_drop = 25;
hook_thickness = 5;

// Coordinate positions
outer_wall_start = -5;
outer_wall_end = 0;

inner_wall_start = glass_slot;
inner_wall_end = glass_slot + hook_thickness;

platform_start = inner_wall_end;

// ======================================
// 4. SENSOR MOUNTING
// ======================================

sensor_hole_spacing = 73.3;
sensor_screw_diameter = 3.2;

post_diameter = 9;
post_height = 8;

// Original offset plus 4mm movement
sensor_edge_clearance = 5.3;
post_offset = 4;

first_hole_x =
    platform_start
    + hook_thickness
    + sensor_edge_clearance
    + sensor_screw_diameter / 2
    + post_offset;

second_hole_x =
    first_hole_x + sensor_hole_spacing;

// ======================================
// 5. MAIN BRACKET
// ======================================

difference() {

    union() {

        // Outer vertical retaining wall
        translate([
            outer_wall_start,
            0,
            -hook_drop
        ])
        cube([
            hook_thickness,
            platform_width,
            hook_drop + platform_thickness
        ]);

        // Top bridge connecting both clamp walls
        translate([
            outer_wall_end,
            0,
            0
        ])
        cube([
            glass_slot + hook_thickness,
            platform_width,
            platform_thickness
        ]);

        // Inner vertical retaining wall
        translate([
            inner_wall_start,
            0,
            -hook_drop
        ])
        cube([
            hook_thickness,
            platform_width,
            hook_drop
        ]);

        // Long horizontal sensor platform
        translate([
            platform_start,
            0,
            0
        ])
        cube([
            platform_length,
            platform_width,
            platform_thickness
        ]);

        // Two sensor mounting posts
        for (x = [first_hole_x, second_hole_x]) {

            translate([
                x,
                platform_width / 2,
                -post_height
            ])
            cylinder(
                h = post_height + platform_thickness,
                d = post_diameter
            );
        }
    }

    // ==================================
    // BLIND M3 SCREW HOLES
    // ==================================

    for (x = [first_hole_x, second_hole_x]) {

        translate([
            x,
            platform_width / 2,
            -post_height - 0.1
        ])
        cylinder(
            h = post_height + 0.1,
            d = sensor_screw_diameter
        );
    }
}

// ======================================
// 6. DIMENSION CHECKS
// ======================================

echo("Glass slot:", glass_slot);
echo("Glass slot target: 5.2mm");

echo("Platform length:", platform_length);
echo("Platform width:", platform_width);

echo("First screw centre:", first_hole_x);
echo("Second screw centre:", second_hole_x);

echo("Screw spacing:",
     second_hole_x - first_hole_x);

echo("Blind screw depth:", post_height);


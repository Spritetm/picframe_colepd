/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain 
 * this notice you can do whatever you want with this stuff. If we meet some day, 
 * and you think this stuff is worth it, you can buy me a beer in return. 
 * ----------------------------------------------------------------------------
*/


$fs=0.2;

pcb_size=[155.5,130,1.6];
holes=[
    [7.5,7.5,0],
    [7.5,65,0],
    [7.5,130-7.5,0],
    [155.5-40.1,7.5,0],
    [155.5-7.5,65,0],
    [155.5-7.5,130-7.5,0],
    [155.5/2,130+5.3,0] //battery compartment screw
];
btns=[
    [64, 92.25, 0],
    [91.5, 92.25, 0]
];
btn_d=10;

//location of the battery terminals
bat_plus_pos=[24.5, 122, 0];
bat_min_pos=[131, 122, 0];
//Size of the pocket for the battery terminal solder points
bat_term_stickout=[3, 6, 1.3];
bat_term_size=[0.65, 12, 12.5];
bat_dia=15; //aa battery
eink_pcb_offset=[16.2, 16.9, 0];
eink_size=[125.4, 99.5, 0.95];
eink_vis_size=[114.9, 85.8, 0]; //visible area size
eink_vis_offset=[5.25, eink_size.y-eink_vis_size.y-4.6, 0];

pcb_center=eink_pcb_offset+eink_vis_offset+eink_vis_size/2;

mat_size=[145,120,3]; //size of actual mat
mat_cutout=[eink_vis_size.x+15, eink_vis_size.y+15,0]; //cutout for mat in case
mat_vis_extra=1; //how much mm more to show than the visible eink area

case_size=[170,143,24];
case_th=1.5;

screw_insert_d=5;
screw_insert_h=4;
screw_insert_post_d=7;
screw_hole_d=8.5;
screw_thread_hole_d=3.5;

tolerance=0.2;

support_d=2.0+tolerance;
support_pos=[0,-30,-6];
support_angle=[30,0,0];

render=2;

if (render==1) {
    intersection() {
        rotate([0,0,00]) union() {
            translate(-pcb_center) pcb();
            translate([0,0,pcb_size.z]) color("beige") mat();
            case_top();
            case_bottom();
            battery_lid();
        }
        translate([0,55,0])cube([200,1,40], center=true);
    }
} else if (render==2) {
    //translate(-pcb_center) pcb();
    //translate([0,0,pcb_size.z]) color("beige") mat();
    //case_top();
    case_bottom();
    //battery_lid();
} else if (render==3) {
    button("R");
    translate([10,10,0]) button("C");
}


module button(txt) {
    difference() {
        union() {
            cylinder(h=8, d=btn_d-tolerance*2);
            cylinder(h=2, d=btn_d+case_th*2);
            translate([-(case_th-tolerance*2)/2,0,0]) cube([(case_th-tolerance*2),btn_d/2+case_th,5]);
        }
        translate([0,0,-1]) cylinder(h=3.5, d=4.5);
        translate([-3,-3,7.5])linear_extrude(height = 1) {
            text(txt, font = "Liberation Sans:style=Bold",size=6);
        }
    }
}

module pcb() {
    difference() {
        cube(pcb_size);
        for (pos=holes) {
            translate(pos+[0,0,-1]) cylinder(h=pcb_size.z+2, d=3.2);
        }
    }
    //Eink
    translate(eink_pcb_offset) color("white") cube(eink_size+[0,0,pcb_size.z]);
    //eink visible area
    translate(eink_pcb_offset+eink_vis_offset) color("magenta") cube(eink_vis_size+[0,0,pcb_size.z+eink_size.z+0.1]);
    translate(bat_plus_pos-[bat_term_size.x/2, bat_term_size.y/2, bat_term_size.z]) cube(bat_term_size);
    translate(bat_min_pos-[bat_term_size.x/2, bat_term_size.y/2, bat_term_size.z]) cube(bat_term_size);
    translate(bat_plus_pos-[-2, 0, bat_dia/2]) rotate([0,90,0]) cylinder(d=bat_dia, h=100);
}

module chamfered_cutout(size) {
    difference() {
    translate([0,0,size.z/2]) cube(size+[size.z*2,size.z*2,tolerance*2], center=true);
        translate([-size.x/2,-size.y,0]) rotate([0,-45,0]) translate([-size.z*2,0,0]) cube([size.z*2, size.y*2, size.z*2]);
        translate([size.x/2,-size.y,0]) rotate([0,45,0]) translate([0,0,-tolerance*2])cube([size.z*2, size.y*2, size.z*2]);
        translate([-size.x,-size.y/2,0]) rotate([45,0,0]) translate([0,-size.z*2,0])cube([size.x*2, size.z*2, size.z*2]);
        translate([-size.x,size.y/2,0]) rotate([-45,0,0]) translate([0,0,-tolerance*2])cube([size.x*2, size.z*2, size.z*2]);
    }
}


//note: assuming the eink visible middle is at [0,0] and the top of the pcb at z=0
module mat() {
    difference() {
        translate([0,0,mat_size.z/2]) cube(mat_size, center=true);
        for (pos=holes) {
            translate(-pcb_center+pos+[0,0,-1]) cylinder(h=mat_size.z+2, d=screw_insert_post_d+tolerance*2);
        }
        //beveled hole
        translate([0,0,eink_size.z-tolerance])chamfered_cutout(eink_vis_size+[mat_vis_extra*2,mat_vis_extra*2,mat_size.z]);
        //eink plate hole
        translate(-pcb_center+eink_pcb_offset-[1, 1, tolerance]) cube(eink_size+[1, 1, tolerance]*2);
        //cutouts for battery contact solder things
        translate(-pcb_center+bat_plus_pos-[bat_term_stickout.x/2, bat_term_stickout.y/2, pcb_size.z]) cube(bat_term_stickout+[0,0,pcb_size.z]);
        translate(-pcb_center+bat_min_pos-[bat_term_stickout.x/2, bat_term_stickout.y/2, tolerance]) cube(bat_term_stickout+[0,0,tolerance]);
    }
}


module case_top() {
    difference() {
        intersection() {
            case();
            translate([-100,-100,-case_th+tolerance*2]) cube([200,200,20]);
        }
        //lip
        translate([-case_size.x/2+case_th/2-tolerance/2, -case_size.y/2+case_th/2-tolerance/2, -case_size.z+tolerance*2]) cube(case_size-[case_th-tolerance,case_th-tolerance,0]);
    }
}


module case_bot_fancy(pullback) {
    edge_off=case_th*2;
    h=bat_dia+case_th*2;
    difference() {
        //square case
        translate([0,0,bat_dia/2+case_th-pullback]) cube([case_size.x-pullback*2,case_size.y-pullback*2,bat_dia+edge_off], center=true);
        //beveled main
        translate([-100, pcb_center.y-bat_plus_pos.y+bat_dia/2, h-pullback]) rotate([-5,0,0]) cube([200,200,20]);
        //beveled at battery
        translate([-100, pcb_center.y-bat_plus_pos.y-bat_dia/2, h-pullback]) rotate([30,0,0]) translate([0,-200,0]) cube([200,200,20]);
        //beveled edges
        translate([case_size.x/2,-case_size.y/2-tolerance,edge_off-pullback]) rotate([0,50,0]) translate([-100,-tolerance*2,0]) cube([100,case_size.y+tolerance*4,case_size.z,]);
        translate([-case_size.x/2,case_size.y/2-tolerance,edge_off-pullback]) rotate([0,50,180]) translate([-100,-tolerance*2,0]) cube([100,case_size.y+tolerance*4,case_size.z,]);

        //lip
        if (pullback==0) difference() {
            translate([0,0,case_th/2+tolerance/2]) cube([case_size.x,case_size.y,case_th+tolerance], center=true);
            translate([0,0,case_th/2+tolerance/2]) cube([case_size.x-case_th,case_size.y-case_th,case_th+tolerance], center=true);
        }
    }
}

module battery_lid_cutout(pullback) {
    translate(-pcb_center) {
        translate([bat_plus_pos.x-bat_term_size.x/2-pullback,bat_min_pos.y-bat_dia/2-pullback,-20+tolerance*(case_th-pullback)]) cube((bat_min_pos-bat_plus_pos)+[bat_term_size.x+pullback*2,50,20]);
        
    }
}


module battery_lid() {
    difference() {
        intersection() {
            difference() {
                //outer case
                rotate([180,0,0]) case_bot_fancy(0);
                //cut out screwholes
                for (pos=holes) {
                    translate(-pcb_center+pos+[0,0,-50-case_th]) cylinder(h=50, d=screw_hole_d);
                    translate(-pcb_center+pos+[0,0,-50+tolerance]) cylinder(h=50, d=screw_thread_hole_d);
                }
            }
            //cut out battery lid itself and half of the inner wall
            battery_lid_cutout(case_th/2-tolerance);
        }
        difference() {
            rotate([180,0,0]) case_bot_fancy(case_th/2-tolerance);
            battery_lid_cutout(-tolerance);
        }
        difference() {
            //inner case
            rotate([180,0,0]) case_bot_fancy(case_th);
            //cut out screwholes
            for (pos=holes) {
                translate(-pcb_center+pos+[0,0,-50]) cylinder(h=50, d=screw_hole_d+case_th*2);
            }
    translate(-pcb_center+[bat_plus_pos.x-bat_term_size.x,bat_min_pos.y+bat_dia/2,-20+tolerance]) cube((bat_min_pos-bat_plus_pos)+[bat_term_size.x*2,case_th,20]);

        }
    }
    
}

module support(pullback) {
    translate(support_pos) rotate(-support_angle+[270,0,0]) translate([0,0,-pullback]) cylinder(h=90,d=support_d+pullback*2);
}

//support(0);

module case_bottom() {
    difference() {
        union() {
            difference() {
                //outer case
                rotate([180,0,0]) case_bot_fancy(0);
                //cut out screwholes
                for (pos=holes) {
                    translate(-pcb_center+pos+[0,0,-50-case_th]) cylinder(h=50, d=screw_hole_d);
                    translate(-pcb_center+pos+[0,0,-50+tolerance]) cylinder(h=50, d=screw_thread_hole_d);
                }
                //cut out battery lid hole and half of the inner wall
                battery_lid_cutout(case_th/2);
                support(0);
            }
            //add other half of the inner wall
            difference() {
                intersection() {
                    rotate([180,0,0]) case_bot_fancy(case_th/2);
                    battery_lid_cutout(case_th);
                }
                battery_lid_cutout(0);
            }
        }
        difference() {
            //inner case
            rotate([180,0,0]) case_bot_fancy(case_th);
            //cut out screwholes
            for (pos=holes) {
                translate(-pcb_center+pos+[0,0,-50]) cylinder(h=50, d=screw_hole_d+case_th*2);
            }
            battery_lid_cutout(case_th);
            hull() {
                support(case_th);
                translate([0,0,-10]) support(case_th);
            }
            for (pos=btns) {
                translate(pos-pcb_center-[0,0,20]) difference() {
                    //button hole
                    cylinder(d=btn_d+case_th*2, h=10);
                    //slot
                    rotate([0,0,-90]) translate([0,-case_th/2,0]) cube([btn_d+case_th*3, case_th,11]);
                }
            }
            //support in center of case back
            translate([0,0,-20]) cylinder(h=20,d=8);
        }
        for (pos=btns) {
            translate(pos-pcb_center-[0,0,20]) cylinder(d=btn_d+tolerance*2, h=20);
        }
    }
}



module case() {
    hpos=pcb_size.z+mat_size.z+case_th+tolerance;
    difference() {
        union(){
            difference() {
                //outer cube
                translate([0,0,-case_size.z/2+hpos]) cube(case_size, center=true);
                //inner cube cutout
                translate([0,0,-case_size.z/2+hpos]) cube(case_size-[case_th, case_th, case_th]*2, center=true);
            }
            for (pos=holes) {
                translate(-pcb_center+pos+[0,0,pcb_size.z]) cylinder(h=screw_insert_h, d=screw_insert_post_d);
            }
        }
        //mat cutout
        translate([0,0,hpos-case_th-tolerance*2]) chamfered_cutout(mat_cutout+[0,0,case_th+tolerance*2]);
        for (pos=holes) {
            translate(-pcb_center+pos+[0,0,pcb_size.z-tolerance]) cylinder(h=screw_insert_h+tolerance, d=screw_insert_d);
        }
    }
}


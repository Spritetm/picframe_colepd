
* Workflow
Uploading:
- html/js: select image, crop, resize, upload to server
- upload php: use C program to convert to EPD.
  Spit out preview (?), add epd to database w/ timestamp

epd:
- Wake up
If Internet:
 * fetch index JSON file (contains OTA plus timestamps of last 10 images)
 * Update files to make internal store match 
 * Update list of image shows: set to 0 for new images
- Take list of image shows; get lowest show count; show newest image with that count.

Flash:
Other shit: 0x10000
OTA1: 0x150000
OTA2: 0x150000
left: 0x150000

img size 134464
11 images -> 10 150000


Case uses M3 inserts: height 4mm dia 5mm
Corresponding screws: M3*6mm


Colors:
- Grab picture in 'normal' situation (daylight, curtains, ...)
- Open in Gimp
- Image -> precision: 16-bit, linear
- Filters -> blur (to get rid of grain)
- Curve -> make darker colors a bit more dark (so it mixes more white in there, making end image lighter)
- Filters -> levels, select black level, select white level


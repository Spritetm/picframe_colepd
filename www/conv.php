#!/usr/bin/php
<?php

function RGBtoHSV($R, $G, $B)    // RGB values:    0-255, 0-255, 0-255
{                                // HSV values:    0-360, 0-100, 0-100
    // Convert the RGB byte-values to percentages
    $R = ($R / 255);
    $G = ($G / 255);
    $B = ($B / 255);

    // Calculate a few basic values, the maximum value of R,G,B, the
    //   minimum value, and the difference of the two (chroma).
    $maxRGB = max($R, $G, $B);
    $minRGB = min($R, $G, $B);
    $chroma = $maxRGB - $minRGB;

    // Value (also called Brightness) is the easiest component to calculate,
    //   and is simply the highest value among the R,G,B components.
    // We multiply by 100 to turn the decimal into a readable percent value.
    $computedV = 100 * $maxRGB;

    // Special case if hueless (equal parts RGB make black, white, or grays)
    // Note that Hue is technically undefined when chroma is zero, as
    //   attempting to calculate it would cause division by zero (see
    //   below), so most applications simply substitute a Hue of zero.
    // Saturation will always be zero in this case, see below for details.
    if ($chroma == 0)
        return array(0, 0, $computedV);

    // Saturation is also simple to compute, and is simply the chroma
    //   over the Value (or Brightness)
    // Again, multiplied by 100 to get a percentage.
    $computedS = 100 * ($chroma / $maxRGB);

    // Calculate Hue component
    // Hue is calculated on the "chromacity plane", which is represented
    //   as a 2D hexagon, divided into six 60-degree sectors. We calculate
    //   the bisecting angle as a value 0 <= x < 6, that represents which
    //   portion of which sector the line falls on.
    if ($R == $minRGB)
        $h = 3 - (($G - $B) / $chroma);
    elseif ($B == $minRGB)
        $h = 1 - (($R - $G) / $chroma);
    else // $G == $minRGB
        $h = 5 - (($B - $R) / $chroma);

    // After we have the sector position, we multiply it by the size of
    //   each sector's arc (60 degrees) to obtain the angle in degrees.
    $computedH = 60 * $h;

    return array($computedH, $computedS, $computedV);
}

$oimg=imagecreatefromjpeg("img.jpg");
$img=imagecreatetruecolor(600, 448);
$bgc=imagecolorallocate($img, 255, 255, 255);
imagefilledrectangle($img, 0, 0, imagesx($img), imagesy($img), $bgc);
$nx=imagesx($img); $ny=imagesy($oimg)*(imagesx($img)/imagesx($oimg));
if ($ny>imagesy($img)) {
	$ny=imagesy($img); $nx=imagesx($oimg)*(imagesy($img)/imagesy($oimg));
}
echo "Resizing to $nx, $ny\n";
$mx=(imagesx($img)-$nx)/2;
$my=(imagesy($img)-$ny)/2;
imagecopyresampled($img, $oimg, $mx, $my, 0, 0, $nx, $ny, imagesx($oimg), imagesy($oimg));

$hdr=pack("VP", 0xfafa1a1a, time());
while (strlen($hdr)!=64) $hdr=$hdr.' ';

//RGB to HSV
/*
for ($y=0; $y<imagesy($img); $y++) {
	for ($x=0; $x<imagesx($img); $x++) {
		$rgb=imagecolorat($img, $x, $y);
		$hsv=RGBtoHSV(($rgb >> 16) & 0xFF, ($rgb >> 8) & 0xFF, $rgb & 0xFF);
		imagesetpixel($img, $x, $y, ($hsv[0]<<16)+($hsv[1]<<8)+$hsv[2]);
	}
}
*/

$rgb=[
	[0,0,0],
	[255,255,255],
	[0,255,0],
	[0,0,255],
	[255,0,0],
	[255,0,255],
	[255,0,128]
];

for ($y=0; $y<448; $y++) {
	for ($x=0; $x<600; $x++) {
		$c=(($x)/32)&7;
		if ($x&1) {
			$hdr=$hdr.chr(($oc<<4)|$c);
		} else {
			$oc=$c;
		}
	}
}

$f=fopen("epd-img.bin", "w");
fwrite($f, $hdr);
fclose($f);

?>
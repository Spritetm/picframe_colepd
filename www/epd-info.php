<?php
/*
Return JSON with the state we expect the EPD controller to be in: what firmware it's supposed
to have and what images are supposed to be in its memory. We also record the battery voltage.

Called using GET: epd-info.php?mac=01234567&bat=2901
*/
require("config.php");

//This returns the app_info SHA hash. If the image is different, this is different.
//Makes the assumption the appinfo magic doesn't appear earlier in the image.
function get_app_image_data($file) {
	$f=fopen(__DIR__."/".$file, "r");
	if (!$f) return "";
	$data=fread($f, 1024*4);
	fclose($f);
	$magicpos=strpos($data, "\x32\x54\xCD\xAB"); //0xABCD5432 little-endian
	if (!$magicpos) return "";
	$shapos=$magicpos+4+4+8+32+32+16+16+32;
	return base64_encode(substr($data,$shapos,32));
}

$mysqli = mysqli_connect("localhost",$username, $pass, $db); 

//Find device based on MAC
$mac="000000000000";
if (isset($_GET["mac"])) $mac=$_GET["mac"];
if (!preg_match("/^[0-9a-fA-F]{12}$/", $mac)) {
	$mac="000000000000";
}

$result = $mysqli->query("SELECT id,tz,fw_upd FROM devices WHERE `mac`='$mac'");
$dev_info=$result->fetch_assoc();
if (!$dev_info) {
	$stmt = $mysqli->prepare("INSERT INTO devices (mac,tz,fw_upd) VALUES (?,?,?)");
	$tz="CST-8";
	$fw_upd="picframe.bin";
	$stmt->bind_param("sss", $mac, $tz, $fw_upd);
	$stmt->execute() || die($stmt->error);
	//re-run query
	$result = $mysqli->query("SELECT id,tz,fw_upd FROM devices WHERE `mac`='$mac'");
	$dev_info=$result->fetch_assoc();
}


//Insert checkin data into db
$stmt = $mysqli->prepare("INSERT INTO checkins (device_id,battery_mv,ip) VALUES (?,?,?)");
$battery_mv=0;
$ip="";
if (isset($_GET["bat"])) $battery_mv=$_GET["bat"];
if (isset($_SERVER['REMOTE_ADDR'])) $ip=$_SERVER['REMOTE_ADDR'];
$stmt->bind_param("iis", $dev_info["id"], $battery_mv, $ip);
$stmt->execute() || die($stmt->error);

//Grab latest 10 images
$image_ids=array();
$result = $mysqli->query("SELECT id FROM images ORDER BY timestamp DESC LIMIT 10");

//Get IDs of last 10 images
for ($i=0; $i<10; $i++) {
	$row=$result->fetch_assoc();
	$image_ids[$i]=$row["id"];
}

$ret=array();
$ret["time"]=time();
$ret["fw_sha"]=get_app_image_data($dev_info["fw_upd"]);
$ret["fw_upd"]=$dev_info["fw_upd"];
$ret["tz"]=$dev_info["tz"];
$ret["images"]=$image_ids;

header("Content-Type: text/json");
echo json_encode($ret);

?>
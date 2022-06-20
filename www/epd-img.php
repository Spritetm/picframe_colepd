<?php
/*
Return an EPD binary. If no argument, return latest EPD binary. If GET argument 'id' is given,
return EPD binary with that ID.

 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain 
 * this notice you can do whatever you want with this stuff. If we meet some day, 
 * and you think this stuff is worth it, you can buy me a beer in return. 
 * ----------------------------------------------------------------------------
*/
require("config.php");

$mysqli = mysqli_connect("localhost",$username, $pass, $db); 

if (isset($_GET["id"])) {
	$id=intval($_GET["id"]);
	$result = $mysqli->query("SELECT epd_bin FROM images WHERE `id`='$id' ORDER BY timestamp DESC LIMIT 1");
} else {
	//get latest
	$result = $mysqli->query("SELECT epd_bin FROM images ORDER BY timestamp DESC LIMIT 1");
}
header("Content-Type: image/epd");
$row=$result->fetch_assoc();
echo $row["epd_bin"];

?>
<?php
/*
Return an EPD binary. If no argument, return latest EPD binary. If GET argument 'id' is given,
return EPD binary with that ID.
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
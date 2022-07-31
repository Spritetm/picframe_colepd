<?php

$db="epd";
$username="epd";
$pass="boTlCO8QAyNB4toS";

//(id, timestamp, orig_name, epd_bin)

if (!isset($_FILES["image"])) {
	header("Location: /index.html");
}
var_dump($_FILES["image"]);

$pngfile=tempnam("/tmp","epd");
$convproc=popen("conv/conv -p \"".$pngfile."\" \"".$_FILES["image"]["tmp_name"]."\"");
$bin=fread($handle, 1024*1024);
pclose($convproc);

$mysqli = mysqli_connect("localhost",$username, $pass, $db); 

$stmt = $mysqli->prepare("INSERT INTO images (orig_name,epd_bin) VALUES (?,?)");
$stmt->bind_param("sb", "", $bin);
$stmt->execute();

header("Content-Type: image/png");
readfile($pngfile);
unlink($pngfile);

?>
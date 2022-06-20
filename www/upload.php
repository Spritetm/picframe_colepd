<?php

require("config.php");

//(id, timestamp, orig_name, epd_bin)

if (!isset($_FILES["image"])) {
	header("Location: /index.html");
}

//system("/bin/cp \"".$_FILES["image"]["tmp_name"]."\" /tmp/img.png");
$pngfile=tempnam("/tmp","epd");
$convproc=popen(__DIR__."/conv/conv -p \"".$pngfile."\" \"".$_FILES["image"]["tmp_name"]."\"", "r");

$mysqli = mysqli_connect("localhost",$username, $pass, $db); 

$stmt = $mysqli->prepare("INSERT INTO images (orig_name,epd_bin) VALUES (?,?)");
$orig_name="";
$null=0;
$stmt->bind_param("sb", $orig_name, $null);
while(!feof($convproc)) {
	$stmt->send_long_data(1, fread($convproc, 1024));
}
$stmt->send_long_data(1, $bin);

$ret=pclose($convproc);

if ($ret!=0) {
	unlink($pngfile);
	exit(1);
}

$stmt->execute() || die($stmt->error);

//header("Content-Type: image/png");
//readfile($pngfile);
echo base64_encode(file_get_contents($pngfile));

unlink($pngfile);

?>
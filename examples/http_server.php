<?php

$srv = new \Laure\HttpServer("0.0.0.0", 9999);
$srv->on("tick", function($serv) {
    // echo "tick\n";
});
$srv->on("request", function( $conn, $resp) use ($srv) {
    // echo "request: " . $data . "\n";
    $resp->json( ["code" => 0, "msg" => "ok", "data" => ["time" => time()]]);
});
$srv->start();
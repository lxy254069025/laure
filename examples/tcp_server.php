<?php
/**
 * examples/tcp_server.php — raw TCP echo
 *
 * Usage:
 *   php examples/tcp_server.php
 *   nc 127.0.0.1 9501
 */
// declare(strict_types=1);

use Laure\Server;
use Laure\Connection;

$server = new \Laure\TcpServer('0.0.0.0', 9999, ['worker_num' => 2]);

$server->on('connect', function (Server $srv, Connection $conn): void {
    echo "[w# connect id={$conn->getId()} from={$conn->getRemoteAddress()}\n";
    $conn->send("Hello from lxxphp worker #{$conn->getId()}\r\n");
});

$server->on('receive', function (Server $srv, Connection $conn, string $data): void {
    $data = trim($data);
    if ($data === 'quit') { $conn->send("Bye!\r\n"); $conn->close(); return; }
    $conn->send("Echo [{$conn->getId()}]: {$data}\r\n");
});

$server->on('close', function (Server $srv, Connection $conn): void {
    echo "[w# close  id={$conn->getId()}\n";
});

echo "TCP listening on 0.0.0.0:9999 \n";
$server->start();

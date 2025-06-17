<div id="global" class="settings">
<?
        $protocol = (!empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off') ? "https://" : "http://";
        $host = $_SERVER['SERVER_NAME'] ?? 'localhost';
        $url = $protocol . $host . ":8089";
        echo '<div>';
        echo '<iframe src="' . htmlspecialchars($url) . '" width="100%" height="1000" frameborder="0" style="border: 1px solid #ccc; border-radius: 8px;"></iframe>';
        echo '</div>';
?>
</div>

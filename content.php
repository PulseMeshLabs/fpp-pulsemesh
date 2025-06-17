<div id="global" class="settings">
<?
        $protocol = (!empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off') ? "https://" : "http://";
        $host = $_SERVER['SERVER_NAME'] ?? 'localhost';
        $url = $protocol . $host . ":8089";
        echo '<h1>PulseMesh Settings</h1>';
        echo '<div style="margin-bottom: 20px;">';
        echo '<iframe src="' . htmlspecialchars($url) . '" width="100%" height="600" frameborder="0" style="border: 1px solid #ccc; border-radius: 4px;"></iframe>';
        echo '</div>';
        echo '<p>Note - don\'t use the legacy settings below unless specifically directed to do so.</p>';
        
        PrintSettingGroup("PulseMeshSettings", "", "", 1, "fpp-pulsemesh");
?>
</div>

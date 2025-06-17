<div id="global" class="settings">
<?
        $protocol = (!empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off') ? "https://" : "http://";
        $host = $_SERVER['SERVER_NAME'] ?? 'localhost';
        $url = $protocol . $host . ":8089";
        
        // Handle restart button click
        if (isset($_POST['restart_pulsemesh'])) {
            $command = 'sudo /home/fpp/media/plugins/fpp-PulseMesh/scripts/restart_pulsemesh.sh --force 2>&1';
            $output = [];
            $return_var = 0;
            exec($command, $output, $return_var);
            
            if ($return_var === 0) {
                echo '<div style="background-color: #d4edda; border: 1px solid #c3e6cb; color: #155724; padding: 10px; margin: 10px 0; border-radius: 4px;">PulseMesh restart initiated successfully.</div>';
            } else {
                $output_text = !empty($output) ? implode('<br>', array_map('htmlspecialchars', $output)) : 'No output returned';
                echo '<div style="background-color: #f8d7da; border: 1px solid #f5c6cb; color: #721c24; padding: 10px; margin: 10px 0; border-radius: 4px;">Error restarting PulseMesh. Please check permissions and script path.<br><strong>Output:</strong><br>' . $output_text . '</div>';
            }
        }
        
        echo '<div>';
        echo '<iframe src="' . htmlspecialchars($url) . '" width="100%" height="1000" frameborder="0" style="border: 1px solid #ccc; border-radius: 8px;"></iframe>';
        
        // Add restart button below iframe
        echo '<div style="margin-top: 10px;">';
        echo '<form method="post" style="display: inline;">';
        echo '<button type="submit" name="restart_pulsemesh">Restart PulseMesh</button>';
        echo '</form>';
        echo '</div>';
        
        echo '</div>';
?>
</div>

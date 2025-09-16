<div id="global" class="settings">
<?
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
        echo '<iframe id="pulsemesh-iframe" width="100%" height="1000" frameborder="0" style="border: 1px solid #ccc; border-radius: 8px;"></iframe>';
        
        // Add restart button below iframe
        echo '<div style="margin-top: 10px;">';
        echo '<form method="post" style="display: inline;">';
        echo '<button type="submit" name="restart_pulsemesh" style="background-color: #007bff; color: white; border: none; padding: 10px 20px; border-radius: 4px; cursor: pointer; font-size: 14px;">Restart PulseMesh</button>';
        echo '</form>';
        echo '</div>';
        
        echo '</div>';
        
        // Add JavaScript to dynamically set iframe source using browser's current location
        echo '<script>
        document.addEventListener("DOMContentLoaded", function() {
            // Get the current protocol and hostname from the browser URL
            var protocol = window.location.protocol;
            var hostname = window.location.hostname;
            
            // Construct the PulseMesh URL using the same host the user is accessing
            var pulsemeshUrl = protocol + "//" + hostname + ":8089";
            
            // Set the iframe source
            var iframe = document.getElementById("pulsemesh-iframe");
            if (iframe) {
                iframe.src = pulsemeshUrl;
            }
        });
        </script>';
?>
</div>

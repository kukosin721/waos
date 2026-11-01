package com.kukosin.app

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.ui.InternalComposeUiApi
import androidx.compose.ui.Modifier
import androidx.compose.material3.Surface
import androidx.compose.ui.platform.WindowRecomposerPolicy
import androidx.compose.ui.platform.createLifecycleAwareWindowRecomposer
import com.kukosin.app.ui.PlayerApp
import com.kukosin.app.ui.UiSettingsManager
import com.kukosin.app.ui.theme.AppTheme
import com.kukosin.app.ui.theme.Ink

class MainActivity : ComponentActivity() {
    @OptIn(InternalComposeUiApi::class)
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // La escala de animación elegida en Ajustes entra al contexto del Recomposer:
        // así afecta a todas las animaciones de Compose, no sólo a las nuevas.
        WindowRecomposerPolicy.setFactory { rootView ->
            rootView.createLifecycleAwareWindowRecomposer(UiSettingsManager.motionDurationScale)
        }
        enableEdgeToEdge()
        setContent {
            AppTheme {
                Surface(modifier = Modifier.fillMaxSize(), color = Ink) {
                    PlayerApp()
                }
            }
        }
    }
}

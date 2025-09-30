package com.homeoffice.trainer;

import android.os.Bundle;
import android.webkit.WebSettings;
import android.webkit.WebView;
import com.getcapacitor.BridgeActivity;

public class MainActivity extends BridgeActivity {
	@Override
	public void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		// Allow mixed content so ws:// (cleartext) can be initiated when app shell considered secure
		try {
			WebView webView = (WebView) this.bridge.getWebView();
			if (webView != null) {
				WebSettings s = webView.getSettings();
				s.setMixedContentMode(WebSettings.MIXED_CONTENT_ALWAYS_ALLOW);
			}
		} catch (Exception ignored) {}
	}
}

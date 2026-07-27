import bb.cascades 1.3

Page {
    id: page
    actionBarVisibility: ChromeVisibility.Hidden

    Container {
        id: root
        layout: DockLayout {}
        background: Color.Black
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill
        // Must be focusable for TouchKeyboardHandler (Cascades docs)
        focusPolicy: FocusPolicy.KeyAndTouch

        Label {
            id: status
            text: "Cascades CKB probe\nSoft-swipe keyboard → magenta + log\nDocuments/TouchProbeCascades.log\n\nTap screen to focus"
            textStyle.color: Color.White
            textStyle.fontSize: FontSize.Large
            textStyle.textAlign: TextAlign.Center
            horizontalAlignment: HorizontalAlignment.Center
            verticalAlignment: VerticalAlignment.Center
            multiline: true
        }

        eventHandlers: [
            TouchKeyboardHandler {
                onTouch: {
                    _app.onTouchKeyboard(event)
                    if (event.touchType == TouchType.Down) {
                        root.background = Color.Magenta
                        status.text = "CKB Down x=" + event.screenX.toFixed(0)
                                   + " y=" + event.screenY.toFixed(0)
                    } else if (event.touchType == TouchType.Move) {
                        root.background = Color.DarkMagenta
                        status.text = "CKB Move x=" + event.screenX.toFixed(0)
                                   + " y=" + event.screenY.toFixed(0)
                                   + " id=" + event.fingerId
                    } else if (event.touchType == TouchType.Up) {
                        root.background = Color.Black
                        status.text = "CKB Up — check log file"
                    }
                }
            }
        ]

        keyListeners: [
            KeyListener {
                onKeyPressed: {
                    _app.onKey(event)
                    status.text = "Key pressed " + event.key
                }
            }
        ]

        onTouch: {
            // ensure focus when user taps glass
            root.requestFocus()
        }

        onCreationCompleted: {
            root.requestFocus()
            _app.logLine("QML ready; requestFocus on root")
        }
    }
}

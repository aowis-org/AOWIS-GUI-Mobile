import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Settings → Map settings. All changes go through the MAP-owned repository.
Rectangle {
    id: page
    signal backRequested()
    signal wireguardRequested()

    property color surfaceColor: "#102535"
    property color raisedColor: "#254354"
    property color pressedColor: "#35586a"
    property color inputColor: "#19384b"
    property color textColor: "#f2f8fa"
    property color mutedColor: "#b5ccd5"
    property color placeholderColor: "#9fb8c4"
    property color accentColor: "#a3ead8"
    property color borderColor: "#6c91a1"
    property color selectedColor: "#346f86"
    property color selectedTextColor: "#ffffff"
    property color successColor: "#a2ebc2"
    property color errorColor: "#ffb7ad"
    property bool rightToLeft: false

    property string section: "overview"
    property string selectedId: ""
    readonly property var provider: findEntry(mobileMapSettings.providers, "id", selectedId)
    readonly property var upstream: findEntry(mobileMapSettings.upstreams, "name", selectedId)
    color: surfaceColor
    LayoutMirroring.enabled: rightToLeft
    LayoutMirroring.childrenInherit: true

    function findEntry(entries, key, value) {
        for (let i = 0; i < entries.length; ++i)
            if (entries[i][key] === value) return entries[i]
        return ({})
    }
    function showSection(next, id) {
        page.forceActiveFocus()
        page.selectedId = id || ""
        page.section = next
        if (scroll.contentItem) scroll.contentItem.contentY = 0
    }
    function reload() { showSection("overview", "") }
    function goBack() {
        if (section === "provider" || section === "addProvider") showSection("providers", "")
        else if (section === "upstream" || section === "addUpstream") showSection("upstreams", "")
        else if (section !== "overview") showSection("overview", "")
        else backRequested()
    }
    function providerField(field, value) {
        return mobileMapSettings.setProviderField(selectedId, field, value)
    }
    function upstreamField(field, value) {
        if (!mobileMapSettings.setUpstreamField(selectedId, field, value)) return false
        if (field === "name") selectedId = value.trim()
        return true
    }
    function routeNames() {
        const result = []
        const names = provider.routes || []
        for (let i = 0; i < names.length; ++i) result.push(names[i])
        return result
    }
    function changeRoute(name, step) {
        const names = routeNames()
        const index = names.indexOf(name)
        if (step === 0) {
            if (index < 0) names.push(name)
            else names.splice(index, 1)
        } else if (index >= 0 && index + step >= 0 && index + step < names.length) {
            names.splice(index, 1)
            names.splice(index + step, 0, name)
        }
        mobileMapSettings.setRoutes(selectedId, names)
    }

    component Heading: Label {
        Layout.fillWidth: true
        color: page.textColor
        font.bold: true
        font.pixelSize: 16
        wrapMode: Text.WordWrap
    }
    component Hint: Label {
        Layout.fillWidth: true
        color: page.mutedColor
        font.pixelSize: 12
        wrapMode: Text.WrapAnywhere
    }
    component Action: Button {
        Layout.fillWidth: true
        palette.buttonText: page.textColor
        background: Rectangle {
            implicitHeight: 44
            radius: 9
            color: parent.down ? page.pressedColor : page.raisedColor
            border.color: parent.activeFocus ? page.accentColor : page.borderColor
            opacity: parent.enabled ? 1 : 0.5
        }
    }
    component Field: TextField {
        Layout.fillWidth: true
        color: page.textColor
        placeholderTextColor: page.placeholderColor
        selectionColor: page.selectedColor
        selectedTextColor: page.selectedTextColor
        selectByMouse: true
        background: Rectangle {
            implicitHeight: 44
            radius: 9
            color: page.inputColor
            border.color: parent.activeFocus ? page.accentColor : page.borderColor
        }
    }
    component SavedField: Field {
        property string savedValue: ""
        property bool committing: false
        signal commit(string value)
        text: savedValue
        onSavedValueChanged: if (!activeFocus) text = savedValue
        onEditingFinished: {
            if (committing) return
            committing = true
            if (text !== savedValue) commit(text)
            text = savedValue
            committing = false
        }
    }
    component Toggle: CheckBox {
        Layout.fillWidth: true
        palette.windowText: page.textColor
        palette.text: page.textColor
    }
    component MapCombo: ComboBox {
        palette.text: page.textColor
        palette.buttonText: page.textColor
        palette.base: page.inputColor
        palette.button: page.raisedColor
        palette.window: page.inputColor
        palette.highlight: page.selectedColor
        palette.highlightedText: page.selectedTextColor
    }
    component MapSpin: SpinBox {
        palette.text: page.textColor
        palette.base: page.inputColor
        palette.button: page.raisedColor
        palette.buttonText: page.textColor
        palette.highlight: page.selectedColor
        palette.highlightedText: page.selectedTextColor
    }
    component TunnelChoice: MapCombo {
        property string profileId: ""
        readonly property var choices: {
            const entries = [{id: "", label: qsTr("Direct (no VPN)")}]
            let found = profileId === ""
            for (const p of wireguardVpnSettings.profiles) {
                entries.push({id: p.id, label: p.name})
                found = found || p.id === profileId
            }
            if (!found) entries.push({id: profileId, label: qsTr("Missing WireGuard profile")})
            return entries
        }
        Layout.fillWidth: true
        textRole: "label"
        valueRole: "id"
        model: choices
        currentIndex: {
            for (let i = 0; i < choices.length; ++i)
                if (choices[i].id === profileId) return i
            return 0
        }
        indicator: UiIcon {
            kind: "down"
            strokeColor: page.textColor
            width: 22; height: 22
            x: page.rightToLeft ? 12 : parent.width - width - 12
            y: (parent.height - height) / 2
        }
        background: Rectangle {
            implicitHeight: 44
            radius: 9
            color: page.inputColor
            border.color: page.borderColor
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 76
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        anchors.bottomMargin: 12
        spacing: 8
        Action {
            text: page.section === "overview" ? qsTr("Back to Settings") : qsTr("Back")
            Layout.fillWidth: false
            onClicked: page.goBack()
        }
        Heading {
            font.pixelSize: 23
            text: page.section === "providers" ? qsTr("Imagery providers")
                  : page.section === "provider" ? (page.provider.name || qsTr("Imagery providers"))
                  : page.section === "upstreams" ? qsTr("MAP upstream servers")
                  : page.section === "upstream" ? (page.upstream.name || qsTr("MAP upstream servers"))
                  : page.section === "addProvider" ? qsTr("Add provider")
                  : page.section === "addUpstream" ? qsTr("Add upstream") : qsTr("Map settings")
        }
        Label {
            visible: mobileMapSettings.error !== ""
            text: mobileMapSettings.error
            color: page.errorColor
            Layout.fillWidth: true
            wrapMode: Text.WrapAnywhere
        }
        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: scroll.availableWidth
                spacing: 10
                enabled: mobileMapSettings.ready

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: page.section === "overview"
                    Action {
                        text: qsTr("Imagery providers") + " (" + mobileMapSettings.providers.length + ")"
                        onClicked: page.showSection("providers", "")
                    }
                    Action {
                        text: qsTr("MAP upstream servers") + " (" + mobileMapSettings.upstreams.length + ")"
                        onClicked: page.showSection("upstreams", "")
                    }
                    Toggle {
                        text: qsTr("Allow direct provider downloads")
                        checked: mobileMapSettings.allowDirectFetch
                        onClicked: {
                            mobileMapSettings.setAllowDirectFetch(checked)
                            checked = Qt.binding(function() { return mobileMapSettings.allowDirectFetch })
                        }
                    }
                    Hint { text: qsTr("Without direct downloads, missing tiles are requested only from configured MAP upstreams. Cached imagery and terrain remain available offline.") }
                    Toggle {
                        text: qsTr("Poor Internet mode")
                        checked: mobileMapSettings.poorInternet
                        onClicked: {
                            mobileMapSettings.setPoorInternet(checked)
                            checked = Qt.binding(function() { return mobileMapSettings.poorInternet })
                        }
                    }
                    Heading { text: qsTr("JPEG quality"); visible: mobileMapSettings.poorInternet }
                    MapSpin {
                        Layout.fillWidth: true
                        visible: mobileMapSettings.poorInternet
                        from: 1; to: 100
                        value: mobileMapSettings.jpegQuality
                        editable: true
                        onValueModified: mobileMapSettings.setJpegQuality(value)
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: page.section === "providers"
                    Repeater {
                        model: mobileMapSettings.providers
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Action {
                                text: modelData.name + (modelData.inherited ? " · " + qsTr("Inherited") : "")
                                onClicked: page.showSection("provider", modelData.id)
                            }
                            Toggle {
                                Layout.fillWidth: false
                                checked: modelData.enabled
                                Accessible.name: modelData.name
                                onClicked: mobileMapSettings.setProviderField(modelData.id, "enabled", checked)
                            }
                        }
                    }
                    Action {
                        text: qsTr("Add provider")
                        onClicked: {
                            newProviderName.text = ""; newProviderUrl.text = ""
                            page.showSection("addProvider", "")
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: page.section === "addProvider"
                    Heading { text: qsTr("Display name") }
                    Field { id: newProviderName }
                    Heading { text: qsTr("Tile URL template") }
                    Field { id: newProviderUrl; placeholderText: qsTr("https://example.org/{z}/{x}/{y}.png"); LayoutMirroring.enabled: false; inputMethodHints: Qt.ImhUrlCharactersOnly }
                    Action {
                        text: qsTr("Add provider")
                        enabled: newProviderName.text.trim() !== "" && newProviderUrl.text.trim() !== ""
                        onClicked: {
                            const id = mobileMapSettings.addProvider(newProviderName.text, newProviderUrl.text)
                            if (id !== "") page.showSection("provider", id)
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: page.section === "provider" && !!page.provider.id
                    Toggle {
                        text: qsTr("Enabled")
                        checked: !!page.provider.enabled
                        onClicked: { page.providerField("enabled", checked); checked = Qt.binding(function() { return !!page.provider.enabled }) }
                    }
                    Hint { visible: !!page.provider.inherited; text: qsTr("Inherited providers are defined by upstream MAP servers. Only local availability and routing can be changed here.") }
                    ColumnLayout {
                        visible: !page.provider.inherited
                        Layout.fillWidth: true
                        Heading { text: qsTr("Display name") }
                        SavedField { savedValue: page.provider.name || ""; onCommit: function(value) { if (page.section === "provider") page.providerField("name", value) } }
                        Heading { text: qsTr("Tile URL template") }
                        SavedField {
                            savedValue: page.provider.url || ""
                            LayoutMirroring.enabled: false
                            inputMethodHints: Qt.ImhUrlCharactersOnly
                            onCommit: function(value) { if (page.section === "provider") page.providerField("url", value) }
                        }
                        Heading { text: qsTr("Minimum zoom") }
                        MapSpin {
                            Layout.fillWidth: true
                            from: 0; to: page.provider.maxZoom === undefined ? 30 : page.provider.maxZoom
                            value: page.provider.minZoom || 0
                            editable: true
                            onValueModified: page.providerField("minZoom", value)
                        }
                        Heading { text: qsTr("Maximum zoom") }
                        MapSpin {
                            Layout.fillWidth: true
                            from: page.provider.minZoom || 0; to: 30
                            value: page.provider.maxZoom === undefined ? 19 : page.provider.maxZoom
                            editable: true
                            onValueModified: page.providerField("maxZoom", value)
                        }
                        Heading { text: qsTr("My fallback") }
                        Hint { text: qsTr("Choose a higher-resolution provider for zooming beyond this provider's maximum zoom. Fallbacks can form a chain and are personal, not upstream settings.") }
                        MapCombo {
                            id: fallbackChoice
                            Layout.fillWidth: true
                            textRole: "label"; valueRole: "id"
                            model: [{id: "", label: qsTr("None (enlarge available tiles)")}].concat(page.provider.fallbackChoices || [])
                            function savedIndex() {
                                const selected = page.provider.fallback || ""
                                for (let i = 0; i < model.length; ++i)
                                    if (model[i].id === selected) return i
                                return 0
                            }
                            currentIndex: savedIndex()
                            onActivated: {
                                mobileMapSettings.setFallback(page.selectedId, currentValue)
                                currentIndex = Qt.binding(function() { return fallbackChoice.savedIndex() })
                            }
                        }
                        Toggle {
                            text: qsTr("Allow remote downloads")
                            checked: !!page.provider.remote
                            onClicked: { page.providerField("remote", checked); checked = Qt.binding(function() { return !!page.provider.remote }) }
                        }
                        Toggle {
                            text: qsTr("Allow lossy tile transfer")
                            checked: !!page.provider.lossy
                            onClicked: { page.providerField("lossy", checked); checked = Qt.binding(function() { return !!page.provider.lossy }) }
                        }
                    }
                    Heading { text: qsTr("Acquisition mode") }
                    MapCombo {
                        Layout.fillWidth: true
                        textRole: "label"; valueRole: "id"
                        model: [{id: "automatic", label: qsTr("Automatic")},
                                {id: "cascade", label: qsTr("Cascade only")}].concat(!page.provider.inherited && mobileMapSettings.allowDirectFetch ? [{id: "direct", label: qsTr("Direct only")}] : [])
                        currentIndex: {
                            const selected = page.provider.mode || "automatic"
                            for (let i = 0; i < model.length; ++i)
                                if (model[i].id === selected) return i
                            return 0
                        }
                        onActivated: mobileMapSettings.setAcquisition(page.selectedId, currentValue)
                    }
                    Heading { text: qsTr("Provider upstream priority") }
                    Hint { text: qsTr("Use global order or select upstreams in priority order. An empty selection uses the global order.") }
                    Action {
                        text: qsTr("Use global order")
                        enabled: page.routeNames().length > 0
                        onClicked: mobileMapSettings.setRoutes(page.selectedId, [])
                    }
                    Repeater {
                        model: page.routeNames()
                        delegate: RowLayout {
                            required property string modelData
                            required property int index
                            Layout.fillWidth: true
                            Hint { text: (index + 1) + ". " + modelData }
                            Action { Layout.fillWidth: false; text: qsTr("Up"); enabled: index > 0; onClicked: page.changeRoute(modelData, -1) }
                            Action { Layout.fillWidth: false; text: qsTr("Down"); enabled: index + 1 < page.routeNames().length; onClicked: page.changeRoute(modelData, 1) }
                            Action { Layout.fillWidth: false; text: qsTr("Remove"); onClicked: page.changeRoute(modelData, 0) }
                        }
                    }
                    Repeater {
                        model: mobileMapSettings.upstreams
                        delegate: Action {
                            required property var modelData
                            visible: page.routeNames().indexOf(modelData.name) < 0
                            text: qsTr("Add") + " · " + modelData.name
                            onClicked: page.changeRoute(modelData.name, 0)
                        }
                    }
                    Action {
                        visible: !page.provider.inherited
                        text: qsTr("Remove provider")
                        onClicked: { removeDialog.targetType = "provider"; removeDialog.targetId = page.selectedId; removeDialog.open() }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: page.section === "upstreams"
                    Hint { text: qsTr("Upstreams are tried in the displayed order. Each can use its own WireGuard profile.") }
                    Repeater {
                        model: mobileMapSettings.upstreams
                        delegate: RowLayout {
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            Action { text: (index + 1) + ". " + modelData.name; onClicked: page.showSection("upstream", modelData.name) }
                            Action { text: qsTr("Up"); Layout.fillWidth: false; enabled: index > 0; onClicked: mobileMapSettings.moveUpstream(modelData.name, -1) }
                            Action { text: qsTr("Down"); Layout.fillWidth: false; enabled: index + 1 < mobileMapSettings.upstreams.length; onClicked: mobileMapSettings.moveUpstream(modelData.name, 1) }
                        }
                    }
                    Action {
                        text: qsTr("Add upstream")
                        onClicked: {
                            newUpstreamName.text = ""; newUpstreamUrl.text = ""; newUpstreamKey.text = ""; newTunnel.profileId = ""
                            page.showSection("addUpstream", "")
                        }
                    }
                    Action { text: qsTr("Manage WireGuard profiles…"); visible: wireguardVpnSettings.engineAvailable; onClicked: page.wireguardRequested() }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: page.section === "addUpstream"
                    Heading { text: qsTr("Upstream name") }
                    Field { id: newUpstreamName; placeholderText: qsTr("site-map"); inputMethodHints: Qt.ImhNoPredictiveText }
                    Hint { text: qsTr("Use a unique name starting with a letter, followed by letters, digits, hyphens or underscores.") }
                    Heading { text: qsTr("Server URL") }
                    Field { id: newUpstreamUrl; placeholderText: qsTr("http://192.168.2.10:8122"); LayoutMirroring.enabled: false; inputMethodHints: Qt.ImhUrlCharactersOnly }
                    Hint { text: qsTr("Use http:// or https://. The default AOWIS MAP server port is 8122; specify another port in the URL if needed.") }
                    Heading { text: qsTr("API key") }
                    Field { id: newUpstreamKey; echoMode: TextInput.Password }
                    Heading { text: qsTr("Connection") }
                    TunnelChoice { id: newTunnel; onActivated: profileId = currentValue }
                    Action {
                        text: qsTr("Add upstream")
                        enabled: newUpstreamName.text.trim() !== "" && newUpstreamUrl.text.trim() !== ""
                        onClicked: {
                            if (mobileMapSettings.addUpstream(newUpstreamName.text, newUpstreamUrl.text, newTunnel.profileId, newUpstreamKey.text))
                                page.showSection("upstream", newUpstreamName.text.trim())
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: page.section === "upstream" && !!page.upstream.name
                    Heading { text: qsTr("Upstream name") }
                    SavedField { savedValue: page.upstream.name || ""; inputMethodHints: Qt.ImhNoPredictiveText; onCommit: function(value) { if (page.section === "upstream") page.upstreamField("name", value) } }
                    Hint { text: qsTr("Use a unique name starting with a letter, followed by letters, digits, hyphens or underscores.") }
                    Heading { text: qsTr("Server URL") }
                    SavedField { savedValue: page.upstream.url || ""; LayoutMirroring.enabled: false; inputMethodHints: Qt.ImhUrlCharactersOnly; onCommit: function(value) { if (page.section === "upstream") page.upstreamField("url", value) } }
                    Hint { text: qsTr("Use http:// or https://. The default AOWIS MAP server port is 8122; specify another port in the URL if needed.") }
                    Heading { text: qsTr("API key") }
                    SavedField { savedValue: page.upstream.key || ""; echoMode: TextInput.Password; onCommit: function(value) { if (page.section === "upstream") page.upstreamField("key", value) } }
                    Heading { text: qsTr("Connection") }
                    TunnelChoice { profileId: page.upstream.profile || ""; onActivated: page.upstreamField("profile", currentValue) }
                    Hint { text: qsTr("Upstream downloads use only the selected tunnel. Cached map data remains available offline.") }
                    Action { text: qsTr("Manage WireGuard profiles…"); visible: wireguardVpnSettings.engineAvailable; onClicked: page.wireguardRequested() }
                    Action { text: qsTr("Remove upstream"); onClicked: { removeDialog.targetType = "upstream"; removeDialog.targetId = page.selectedId; removeDialog.open() } }
                }
            }
        }
    }
    Dialog {
        id: removeDialog
        property string targetType: ""
        property string targetId: ""
        anchors.centerIn: parent
        width: Math.min(page.width - 32, 340)
        modal: true
        title: qsTr("Confirm removal")
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Label {
            text: qsTr("Remove %1?").arg(removeDialog.targetType === "provider" ? (page.provider.name || "") : removeDialog.targetId)
            color: page.textColor
            wrapMode: Text.WrapAnywhere
        }
        onAccepted: {
            if (targetType === "provider") {
                if (mobileMapSettings.removeProvider(targetId)) page.showSection("providers", "")
            } else if (mobileMapSettings.removeUpstream(targetId)) page.showSection("upstreams", "")
        }
    }
}

import QtQuick
import QtQuick.Controls
import ".."

// Primary 4-tab navigation. Contract preserved: `currentIndex` + tabSelected(int).
Item {
    id: root

    readonly property real safeBottom: (typeof androidContext !== "undefined" && androidContext) ? androidContext.safeInsetBottom : 0
    height: Theme.dp(Theme.navBarHeight) + safeBottom
    property int currentIndex: 1 // Default to Files

    signal tabSelected(int index)

    readonly property var tabs: [
        { name: "Home",     icon: "home"   },
        { name: "Files",    icon: "folder" },
        { name: "Tools",    icon: "tune"   },
        { name: "Settings", icon: "gear"   }
    ]

    Rectangle {
        anchors.fill: parent
        color: Theme.surface1

        // Hairline on the top edge only — a full border reads as a boxed-in
        // strip on a phone; the single edge keeps it feeling like a floating bar.
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Theme.borderSubtle
        }

        Item {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: Theme.dp(Theme.navBarHeight)

            Row {
                anchors.fill: parent

            Repeater {
                model: root.tabs

                Item {
                    id: tabItem
                    width: root.width / root.tabs.length
                    height: parent.height

                    readonly property bool active: root.currentIndex === index

                    // Active pill behind the icon
                    Rectangle {
                        id: pill
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        anchors.topMargin: Theme.dp(8)
                        width: Theme.dp(64)
                        height: Theme.dp(30)
                        radius: Theme.radiusPill
                        color: tabItem.active ? Theme.alpha(Theme.accentEnd, 0.18) : "transparent"
                        border.color: tabItem.active ? Theme.alpha(Theme.accentEnd, 0.35) : "transparent"
                        border.width: tabItem.active ? 1 : 0

                        Behavior on color { ColorAnimation { duration: Theme.durMed } }
                    }

                    Icon {
                        id: tabIcon
                        anchors.centerIn: pill
                        name: modelData.icon
                        size: Theme.dp(Theme.iconMd)
                        weight: tabItem.active ? 2.0 : Icons.stroke
                        color: tabItem.active ? Theme.accentInk : Theme.textMuted

                        Behavior on color { ColorAnimation { duration: Theme.durMed } }
                    }

                    Text {
                        anchors.top: pill.bottom
                        anchors.topMargin: Theme.dp(3)
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: modelData.name
                        font.pixelSize: Theme.dp(Theme.fontSm)
                        font.weight: tabItem.active ? Font.DemiBold : Font.Normal
                        color: tabItem.active ? Theme.textPrimary : Theme.textMuted

                        Behavior on color { ColorAnimation { duration: Theme.durMed } }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            if (root.currentIndex !== index) {
                                root.currentIndex = index
                                root.tabSelected(index)
                            }
                        }
                    }
                }
            }
        }
    }
}
}

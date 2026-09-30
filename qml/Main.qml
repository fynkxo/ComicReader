import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 1024
    height: 768
    visible: true
    title: qsTr("Comic Reader")
    color: "#1e1e1e"

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            Label {
                text: qsTr("Library")
                font.pixelSize: 20
                color: "#ffffff"
            }
        }
    }

    // 书架占位区域（后续替换为真正的书架视图）
    Rectangle {
        anchors.fill: parent
        color: "#2b2b2b"

        Column {
            anchors.centerIn: parent
            spacing: 12

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Your comic library will appear here")
                color: "#ffffff"
                font.pixelSize: 18
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("MVP skeleton — Comic Reader")
                color: "#888888"
                font.pixelSize: 13
            }
        }
    }
}

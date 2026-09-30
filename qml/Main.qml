import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 1024
    height: 768
    visible: true
    title: controller && controller.pageCount > 0
           ? qsTr("Comic Reader") + " - " + controller.comicName
           : qsTr("Comic Reader")
    color: "#1e1e1e"

    // 由 C++ 注入的阅读控制器
    property var controller: null

    header: ToolBar {
        visible: !root.readerOpen
        RowLayout {
            anchors.fill: parent
            Label {
                text: qsTr("Library")
                font.pixelSize: 20
                color: "#ffffff"
                Layout.fillWidth: true
            }
        }
    }

    // 阅读器是否打开（命令行指定漫画时初始为 true）
    property bool readerOpen: initialComicLoaded === true

    // 书架 / 阅读器切换
    StackLayout {
        anchors.fill: parent
        currentIndex: root.readerOpen ? 1 : 0

        // ---- 书架占位 ----
        Rectangle {
            color: "#2b2b2b"

            Column {
                anchors.centerIn: parent
                spacing: 14

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Your comic library will appear here")
                    color: "#ffffff"
                    font.pixelSize: 18
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Open a .zip / .cbz archive or an image folder to start reading")
                    color: "#aaaaaa"
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    width: 480
                    wrapMode: Text.WordWrap
                }

                // 打开按钮：使用系统文件对话框选择漫画
                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Open Comic...")
                    onClicked: root.fileDialog.open()
                }

                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: controller ? controller.statusMessage : ""
                    color: "#88cc88"
                    font.pixelSize: 12
                }
            }
        }

        // ---- 阅读器 ----
        ReaderView {
            controller: root.controller
        }
    }

    // 系统文件对话框：选择 ZIP/CBZ 归档
    FileDialog {
        id: fileDialog
        title: qsTr("Open Comic")
        nameFilters: [qsTr("Comic archives (*.zip *.cbz)"), qsTr("All files (*)")]
        onAccepted: {
            if (root.controller && root.controller.openComic(selectedFile.toString().replace("file:///", "")))
                root.readerOpen = true
        }
    }

    // 目录选择对话框
    FolderDialog {
        id: folderDialog
        title: qsTr("Open Image Folder")
        onAccepted: {
            if (root.controller && root.controller.openComic(selectedFolder.toString().replace("file:///", "")))
                root.readerOpen = true
        }
    }

    // 快捷键：Ctrl+O 打开归档，Ctrl+Shift+O 打开文件夹
    Shortcut {
        sequences: [StandardKey.Open]
        onActivated: fileDialog.open()
    }

    Shortcut {
        sequence: "Ctrl+Shift+O"
        onActivated: folderDialog.open()
    }

    // 打开文件夹入口
    Rectangle {
        anchors { top: parent.top; right: parent.right; topMargin: 8; rightMargin: 12 }
        width: openFolderBtn.width
        height: openFolderBtn.height
        color: "transparent"
        visible: !root.readerOpen

        Button {
            id: openFolderBtn
            text: qsTr("Open Folder...")
            onClicked: folderDialog.open()
        }
    }
}


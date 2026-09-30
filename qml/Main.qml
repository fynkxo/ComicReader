import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 1024
    height: 768
    visible: true
    title: appController && appController.pageCount > 0
           ? qsTr("Comic Reader") + " - " + appController.comicName
           : qsTr("Comic Reader")
    color: "#1e1e1e"

    // 注意：appController / appLibrary / appCover 由 C++ 通过
    // QQmlContext::setContextProperty 注入。不要在此处声明同名 property，
    // 否则会遮蔽(context shadow)上下文属性，导致 QML 侧始终得到 null。

    header: ToolBar {
        id: libraryBar
        visible: !root.readerOpen
        // Basic 样式默认浅色背景，需显式设为深色以配合白色文字
        background: Rectangle {
            color: "#333333"
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12

            Label {
                text: qsTr("Library")
                font.pixelSize: 20
                color: "#ffffff"
                Layout.fillWidth: true
            }

            Label {
                text: appLibrary
                      ? qsTr("%n comic(s)", "", appLibrary.count)
                      : ""
                color: "#909090"
                font.pixelSize: 12
            }

            Button {
                text: qsTr("Add Folder...")
                onClicked: scanFolderDialog.open()
            }

            Button {
                text: qsTr("Open File...")
                onClicked: fileDialog.open()
            }

            Button {
                text: qsTr("Refresh")
                enabled: appLibrary !== null
                onClicked: {
                    if (appCover) appCover.clearCache()
                    appLibrary.refresh()
                }
            }
        }
    }

    // 阅读器是否打开（命令行指定漫画时初始为 true）
    property bool readerOpen: initialComicLoaded === true

    // 书架 / 阅读器切换
    StackLayout {
        anchors.fill: parent
        currentIndex: root.readerOpen ? 1 : 0

        // ---- 书架 ----
        LibraryView {
            comicModel: appLibrary
            controller: appController

            onComicActivated: function (path) {
                if (appController && appController.openComic(path))
                    root.readerOpen = true
            }
            onAddFolderRequested: scanFolderDialog.open()
        }

        // ---- 阅读器 ----
        ReaderView {
            controller: appController
        }
    }

    // 系统文件对话框：选择 ZIP/CBZ 归档
    FileDialog {
        id: fileDialog
        title: qsTr("Open Comic")
        nameFilters: [qsTr("Comic archives (*.zip *.cbz)"), qsTr("All files (*)")]
        onAccepted: {
            if (appController && appController.openComic(selectedFile.toString().replace("file:///", "")))
                root.readerOpen = true
        }
    }

    // 目录选择对话框：扫描并导入漫画
    FolderDialog {
        id: scanFolderDialog
        title: qsTr("Add Comic Folder")
        onAccepted: {
            const path = selectedFolder.toString().replace("file:///", "")
            if (appLibrary && appLibrary.scanFolder(path)) {
                if (appCover) appCover.clearCache()
                appLibrary.refresh()
            }
        }
    }

    // 目录选择对话框：直接以图片文件夹方式阅读
    FolderDialog {
        id: folderDialog
        title: qsTr("Open Image Folder")
        onAccepted: {
            if (appController && appController.openComic(selectedFolder.toString().replace("file:///", "")))
                root.readerOpen = true
        }
    }

    // 快捷键：Ctrl+O 打开归档，Ctrl+Shift+O 打开图片文件夹，Ctrl+R 刷新书架
    Shortcut {
        sequences: [StandardKey.Open]
        onActivated: fileDialog.open()
    }

    Shortcut {
        sequence: "Ctrl+Shift+O"
        onActivated: folderDialog.open()
    }

    Shortcut {
        sequence: "Ctrl+R"
        onActivated: {
            if (appLibrary) {
                if (appCover) appCover.clearCache()
                appLibrary.refresh()
            }
        }
    }

    // 状态提示条
    footer: ToolBar {
        background: Rectangle { color: "#2a2a2a" }
        visible: !root.readerOpen && appController && appController.statusMessage !== ""
        height: visible ? 28 : 0
        Label {
            anchors.fill: parent
            anchors.leftMargin: 12
            verticalAlignment: Text.AlignVCenter
            text: appController ? appController.statusMessage : ""
            color: "#88cc88"
            font.pixelSize: 12
        }
    }
}


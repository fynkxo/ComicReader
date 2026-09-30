import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/// 书架视图：网格展示数据库中的漫画，含封面、标题、作者与阅读进度
Item {
    id: library

    // 注意：属性不能命名为 "model"，那是 QML 保留名，会导致绑定失效
    property var comicModel: null   ///< LibraryModel
    property var controller: null   ///< 阅读控制器

    /// 请求添加漫画文件夹（由外部连接到目录对话框）
    signal addFolderRequested()

    /// 用户点击某本漫画
    signal comicActivated(string path)

    implicitWidth: 800
    implicitHeight: 600

    /// 卡片宽度随窗口自适应
    readonly property int cardWidth: Math.max(
        120, Math.floor((library.width - 60)
            / Math.max(2, Math.floor(library.width / 220))))

    Rectangle {
        anchors.fill: parent
        color: "#2b2b2b"
    }

    // 空书架提示
    Column {
        anchors.centerIn: parent
        spacing: 14
        visible: !library.comicModel || library.comicModel.count === 0

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Your comic library is empty")
            color: "#ffffff"
            font.pixelSize: 18
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Add a folder to scan for .zip / .cbz archives")
            color: "#aaaaaa"
            font.pixelSize: 13
        }

        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Add Folder...")
            onClicked: library.addFolderRequested()
        }
    }

    // 漫画网格
    GridView {
        id: grid
        anchors.fill: parent
        anchors.margins: 16
        visible: library.comicModel && library.comicModel.count > 0
        model: library.comicModel
        clip: true
        cellWidth: library.cardWidth + 16
        cellHeight: Math.round(library.cardWidth * 1.5) + 58
        boundsBehavior: Flickable.StopAtBounds

        delegate: Rectangle {
            id: card
            width: library.cardWidth
            height: grid.cellHeight - 16
            color: "transparent"

            property bool hovered: hoverArea.containsMouse

            Column {
                anchors.fill: parent
                spacing: 4

                // ---- 封面 ----
                Rectangle {
                    id: coverBox
                    width: parent.width
                    height: Math.round(width * 1.4)
                    radius: 4
                    color: "#1a1a1a"
                    border.color: card.hovered ? "#5a9fd4" : "#3a3a3a"
                    border.width: card.hovered ? 2 : 1
                    clip: true

                    Image {
                        id: cover
                        anchors.fill: parent
                        anchors.margins: 1
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        // 缓存开关置 false：封面由 C++ 侧统一缓存，
                        // 否则滚动出视口再回来会重复请求解码
                        cache: false
                        smooth: true
                        source: library.comicModel && comicId > 0
                                ? "image://cover/" + comicId : ""
                    }

                    // 阅读进度条
                    Rectangle {
                        visible: hasProgress
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                        height: 4
                        color: "#33000000"

                        Rectangle {
                            width: parent.width * progress
                            height: parent.height
                            color: "#4a9eff"
                        }
                    }

                    // 页码角标
                    Rectangle {
                        anchors { right: parent.right; top: parent.top; margins: 4 }
                        width: pageLabel.width + 10
                        height: pageLabel.height + 4
                        radius: 3
                        color: "#aa000000"
                        visible: hasProgress

                        Label {
                            id: pageLabel
                            anchors.centerIn: parent
                            text: (currentPage + 1) + "/" + pageCount
                            color: "white"
                            font.pixelSize: 10
                        }
                    }
                }

                // ---- 标题 ----
                Text {
                    width: parent.width
                    text: title
                    color: "#ffffff"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    maximumLineCount: 2
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                }

                // ---- 作者 / 系列 ----
                Text {
                    width: parent.width
                    text: writer || series
                    color: "#909090"
                    font.pixelSize: 10
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    visible: text.length > 0
                }
            }

            MouseArea {
                id: hoverArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: library.comicActivated(path)
            }
        }
    }
}

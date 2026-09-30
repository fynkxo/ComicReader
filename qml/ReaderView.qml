import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/// 阅读器容器：根据设置在单页 / 双页 / 条漫三种模式间切换
Item {
    id: reader

    property var controller: null

    /// 阅读模式常量
    readonly property int modeSingle: 0
    readonly property int modeDouble: 1
    readonly property int modeWebtoon: 2
    property int readingMode: modeSingle

    implicitWidth: 800
    implicitHeight: 600

    function setMode(mode) {
        reader.readingMode = mode
    }

    // 顶部工具栏
    Rectangle {
        id: topBar
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: 44
        color: "#cc1e1e1e"
        visible: reader.controller && reader.controller.pageCount > 0

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 10

            ToolButton {
                // 工具栏按钮不参与 Tab 焦点链，避免抢走阅读区键盘焦点
                focusPolicy: Qt.NoFocus
                text: qsTr("Back")
                onClicked: reader.controller.closeComic()
            }

            Label {
                text: reader.controller ? reader.controller.comicName : ""
                color: "white"
                elide: Text.ElideMiddle
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
            }

            Button {
                focusPolicy: Qt.NoFocus
                text: qsTr("Single")
                checkable: true
                checked: reader.readingMode === reader.modeSingle
                onClicked: reader.setMode(reader.modeSingle)
            }

            Button {
                focusPolicy: Qt.NoFocus
                text: qsTr("Double")
                checkable: true
                checked: reader.readingMode === reader.modeDouble
                onClicked: reader.setMode(reader.modeDouble)
            }

            Button {
                focusPolicy: Qt.NoFocus
                text: qsTr("Webtoon")
                checkable: true
                checked: reader.readingMode === reader.modeWebtoon
                onClicked: reader.setMode(reader.modeWebtoon)
            }
        }
    }

    // 阅读区域：避开上下工具栏
    Item {
        id: viewArea
        anchors {
            top: topBar.visible ? topBar.bottom : parent.top
            bottom: bottomBar.visible ? bottomBar.top : parent.bottom
            left: parent.left
            right: parent.right
        }

        Loader {
            anchors.fill: parent
            source: reader.readingMode === reader.modeWebtoon
                    ? "qrc:/qml/WebtoonView.qml"
                    : (reader.readingMode === reader.modeDouble
                       ? "qrc:/qml/DoublePageView.qml"
                       : "qrc:/qml/SinglePageView.qml")
            onLoaded: if (item) item.controller = reader.controller
        }
    }

    // 底部翻页栏（条漫模式自带页码指示器，隐藏此栏）
    Rectangle {
        id: bottomBar
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
        height: 52
        color: "#cc1e1e1e"
        visible: reader.controller && reader.controller.pageCount > 0
                 && reader.readingMode !== reader.modeWebtoon

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 12

            Button {
                focusPolicy: Qt.NoFocus
                text: qsTr("Previous")
                enabled: reader.controller && reader.controller.currentPage > 0
                onClicked: reader.controller.previousPage()
            }

            Label {
                text: reader.controller
                      ? (reader.controller.currentPage + 1) + " / "
                        + reader.controller.pageCount
                      : ""
                color: "white"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
            }

            Button {
                focusPolicy: Qt.NoFocus
                text: qsTr("Next")
                enabled: reader.controller
                           && reader.controller.currentPage < reader.controller.pageCount - 1
                onClicked: reader.controller.nextPage()
            }
        }
    }
}

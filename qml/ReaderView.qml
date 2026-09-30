import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/// 单页阅读界面：支持翻页、缩放拖拽、适应屏幕
Item {
    id: reader

    property var controller: null

    implicitWidth: 800
    implicitHeight: 600

    // 缩放与平移状态
    property real zoom: 1.0
    property real fitScale: 1.0
    property real offsetX: 0
    property real offsetY: 0

    function clampOffsets() {
        var maxX = Math.max(0, (reader.width * reader.zoom - reader.width) / 2)
        var maxY = Math.max(0, (reader.height * reader.zoom - reader.height) / 2)
        reader.offsetX = Math.max(-maxX, Math.min(maxX, reader.offsetX))
        reader.offsetY = Math.max(-maxY, Math.min(maxY, reader.offsetY))
    }

    function resetView() {
        reader.zoom = reader.fitScale
        reader.offsetX = 0
        reader.offsetY = 0
    }

    function zoomBy(factor) {
        reader.zoom = Math.max(reader.fitScale, Math.min(8.0, reader.zoom * factor))
        reader.clampOffsets()
    }

    // 页面切换后重置视图
    Connections {
        target: reader.controller
        function onCurrentPageChanged() { reader.resetView() }
    }

    Connections {
        target: reader.controller
        function onComicChanged() { reader.resetView() }
    }

    Rectangle {
        anchors.fill: parent
        color: "#1a1a1a"

        Flickable {
            id: flick
            anchors.fill: parent
            anchors.margins: 8
            contentWidth: width * reader.zoom
            contentHeight: height * reader.zoom
            clip: true
            interactive: reader.zoom > reader.fitScale
            boundsBehavior: Flickable.StopAtBounds

            Image {
                id: pageImage
                anchors.centerIn: parent
                width: reader.zoom
                height: reader.zoom
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: true
                smooth: true
                source: reader.controller && reader.controller.pageCount > 0
                        ? "image://comicpage/" + reader.controller.currentPage
                        : ""

                // 根据图片原始尺寸计算适应比例
                onStatusChanged: {
                    if (status === Image.Ready && sourceSize.width > 0) {
                        reader.fitScale = Math.min(flick.width / sourceSize.width,
                                                   flick.height / sourceSize.height)
                        reader.zoom = reader.fitScale
                    }
                }
            }

            // 滚轮缩放
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.NoButton
                onWheel: function (wheel) {
                    if (reader.controller && reader.controller.pageCount > 0) {
                        reader.zoomBy(wheel.angleDelta.y > 0 ? 1.15 : 1 / 1.15)
                        wheel.accepted = true
                    }
                }
            }
        }
    }

    // 缩放时允许拖拽平移
    MouseArea {
        id: dragArea
        anchors.fill: parent
        anchors.margins: 8
        enabled: reader.zoom > reader.fitScale
        acceptedButtons: Qt.LeftButton
        property real lastX: 0
        property real lastY: 0
        onPressed: function (mouse) { dragArea.lastX = mouse.x; dragArea.lastY = mouse.y }
        onPositionChanged: function (mouse) {
            if (pressed) {
                reader.offsetX += mouse.x - dragArea.lastX
                reader.offsetY += mouse.y - dragArea.lastY
                reader.clampOffsets()
                dragArea.lastX = mouse.x
                dragArea.lastY = mouse.y
            }
        }
    }

    // 顶部信息栏
    Rectangle {
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: 44
        color: "#cc1e1e1e"
        visible: reader.controller && reader.controller.pageCount > 0

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 12

            ToolButton {
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

            ToolButton {
                text: qsTr("Fit")
                enabled: reader.zoom > reader.fitScale
                onClicked: reader.resetView()
            }

            ToolButton {
                text: qsTr("Zoom In")
                onClicked: reader.zoomBy(1.25)
            }

            ToolButton {
                text: qsTr("Zoom Out")
                onClicked: reader.zoomBy(1 / 1.25)
            }
        }
    }

    // 底部翻页栏
    Rectangle {
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
        height: 52
        color: "#cc1e1e1e"
        visible: reader.controller && reader.controller.pageCount > 0

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 12

            Button {
                text: qsTr("Previous")
                enabled: reader.controller && reader.controller.currentPage > 0
                onClicked: reader.controller.previousPage()
            }

            Label {
                text: reader.controller
                      ? (reader.controller.currentPage + 1) + " / " + reader.controller.pageCount
                      : ""
                color: "white"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
            }

            Button {
                text: qsTr("Next")
                enabled: reader.controller
                           && reader.controller.currentPage < reader.controller.pageCount - 1
                onClicked: reader.controller.nextPage()
            }
        }
    }

    // 键盘快捷键：左右翻页，+/-/0 缩放
    Keys.onLeftPressed: if (reader.controller) reader.controller.previousPage()
    Keys.onRightPressed: if (reader.controller) reader.controller.nextPage()
    Keys.onPressed: function (event) {
        if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal)
            reader.zoomBy(1.25)
        else if (event.key === Qt.Key_Minus)
            reader.zoomBy(1 / 1.25)
        else if (event.key === Qt.Key_0)
            reader.resetView()
        else
            event.accepted = false
    }
    focus: true
}

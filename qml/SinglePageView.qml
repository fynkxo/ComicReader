import QtQuick
import QtQuick.Controls

/// 单页阅读视图：适应屏幕，支持缩放与拖拽平移
Item {
    id: single

    property var controller: null

    /// 用户缩放倍率（1 = 适应屏幕）
    property real zoom: 1.0
    /// 平移偏移（像素）
    property real offsetX: 0
    property real offsetY: 0

    implicitWidth: 800
    implicitHeight: 600

    /// 页面原始尺寸（由控制器提供）
    property size sourceSize: controller && controller.pageCount > 0
                              ? controller.pageSourceSize(controller.currentPage)
                              : Qt.size(0, 0)

    /// 适应屏幕的显示尺寸
    property size fitSize: {
        if (sourceSize.width <= 0 || sourceSize.height <= 0)
            return Qt.size(0, 0)
        const s = Math.min(width / sourceSize.width, height / sourceSize.height)
        return Qt.size(Math.round(sourceSize.width * s), Math.round(sourceSize.height * s))
    }

    /// 实际显示尺寸
    property size displaySize: Qt.size(Math.round(fitSize.width * zoom),
                                       Math.round(fitSize.height * zoom))

    function clampOffsets() {
        const maxX = Math.max(0, (displaySize.width - width) / 2)
        const maxY = Math.max(0, (displaySize.height - height) / 2)
        offsetX = Math.max(-maxX, Math.min(maxX, offsetX))
        offsetY = Math.max(-maxY, Math.min(maxY, offsetY))
    }

    function resetView() {
        zoom = 1.0
        offsetX = 0
        offsetY = 0
    }

    function zoomBy(factor) {
        zoom = Math.max(1.0, Math.min(8.0, zoom * factor))
        clampOffsets()
    }

    // 页面/漫画/尺寸变化后重置视图
    Connections {
        target: single.controller
        function onCurrentPageChanged() { single.resetView() }
    }
    Connections {
        target: single.controller
        function onComicChanged() { single.resetView() }
    }
    onSourceSizeChanged: single.resetView()

    Rectangle {
        anchors.fill: parent
        color: "#141414"
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: Math.max(width, single.displaySize.width)
        contentHeight: Math.max(height, single.displaySize.height)
        clip: true
        // 仅在放大后才允许拖拽
        interactive: single.zoom > 1.0
        boundsBehavior: Flickable.StopAtBounds
        // 关闭惯性，缩放状态下手感更可控
        flickDeceleration: 4000

        Image {
            id: pageImage
            // 关键修复：使用 displaySize（像素尺寸），并应用平移偏移
            x: Math.round((flick.contentWidth - single.displaySize.width) / 2 + single.offsetX)
            y: Math.round((flick.contentHeight - single.displaySize.height) / 2 + single.offsetY)
            width: single.displaySize.width
            height: single.displaySize.height
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            cache: true
            smooth: true
            visible: single.displaySize.width > 0
            source: single.controller && single.controller.pageCount > 0
                    ? "image://comicpage/" + single.controller.currentPage
                    : ""
        }

        // 滚轮缩放
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            onWheel: function (wheel) {
                if (single.controller && single.controller.pageCount > 0) {
                    single.zoomBy(wheel.angleDelta.y > 0 ? 1.15 : 1 / 1.15)
                    wheel.accepted = true
                }
            }
        }
    }

    // 缩放控制浮层
    Rectangle {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
        width: 40
        height: 132
        radius: 8
        color: "#99000000"
        visible: single.controller && single.controller.pageCount > 0

        Column {
            anchors.centerIn: parent
            spacing: 4

            ToolButton {
                text: "+"
                width: 32
                onClicked: single.zoomBy(1.25)
            }

            Label {
                text: Math.round(single.zoom * 100) + "%"
                color: "white"
                font.pixelSize: 10
                horizontalAlignment: Text.AlignHCenter
                width: 36
            }

            ToolButton {
                text: "−"
                width: 32
                enabled: single.zoom > 1.0
                onClicked: single.zoomBy(1 / 1.25)
            }

            ToolButton {
                text: "⤢"
                width: 32
                enabled: single.zoom > 1.0
                onClicked: single.resetView()
            }
        }
    }

    // 键盘
    Keys.onPressed: function (event) {
        if (!single.controller || single.controller.pageCount <= 0) {
            event.accepted = false
            return
        }
        if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) {
            single.zoomBy(1.25)
        } else if (event.key === Qt.Key_Minus) {
            single.zoomBy(1 / 1.25)
        } else if (event.key === Qt.Key_0) {
            single.resetView()
        } else {
            event.accepted = false
        }
    }
    focus: true
}

import QtQuick
import QtQuick.Controls

/// 双页并排视图（日漫模式）
///
/// spread = 0  : 第 1、2 页并排
/// spread = 1  : 第 3、4 页并排 ……以此类推
///
/// 日漫（右开）习惯：跨页时右页为奇数页（1,3,5…），左页为偶数页。
/// 可通过 rightToLeft 属性关闭该行为。
Item {
    id: spreadView

    property var controller: null
    /// 宽高比明显偏宽的跨页图（由控制器启发式判断时可覆盖）
    property bool isWideSpread: false
    /// 是否右起阅读（日漫）
    property bool rightToLeft: true

    implicitWidth: 800
    implicitHeight: 600

    readonly property int spread: spreadView.controller
                                   ? Math.floor(spreadView.controller.currentPage / 2) : 0
    readonly property int leftIndex: spreadView.isWideSpread
                                     ? spreadView.controller.currentPage
                                     : spreadView.spread * 2
    readonly property int rightIndex: spreadView.isWideSpread
                                      ? spreadView.controller.currentPage + 1
                                      : spreadView.spread * 2 + 1
    readonly property bool hasLeft: spreadView.leftIndex >= 0
                                    && spreadView.controller
                                    && spreadView.leftIndex < spreadView.controller.pageCount
    readonly property bool hasRight: spreadView.rightIndex >= 0
                                     && spreadView.controller
                                     && spreadView.rightIndex < spreadView.controller.pageCount

    /// 跨页起点 = 左页原始索引；日漫右起时该跨页属于后一个 spread
    readonly property int spreadStartIndex: spreadView.isWideSpread
                                           ? spreadView.leftIndex
                                           : (spreadView.rightToLeft
                                              ? spreadView.leftIndex + 1
                                              : spreadView.leftIndex)

    /// 取当前跨页中任一可用页的原始尺寸（用于计算适应比例）
    function baseSourceSize() {
        if (!controller || controller.pageCount <= 0)
            return Qt.size(0, 0)
        if (hasRight)
            return controller.pageSourceSize(rightIndex)
        if (hasLeft)
            return controller.pageSourceSize(leftIndex)
        return Qt.size(0, 0)
    }

    /// 单页槽位宽度（双页时为半宽）
    readonly property real slotWidth: (hasLeft && hasRight) ? (width - 2) / 2 : width

    /// 每页实际显示尺寸（按原始宽高比适应槽位）
    property size slotSize: {
        const base = baseSourceSize()
        if (!base || base.width <= 0 || base.height <= 0)
            return Qt.size(0, 0)
        const s = Math.min(slotWidth / base.width, height / base.height)
        return Qt.size(Math.round(base.width * s), Math.round(base.height * s))
    }

    Rectangle {
        anchors.fill: parent
        color: "#141414"
    }

    Row {
        anchors.centerIn: parent
        spacing: 2

        // 左槽位
        Item {
            width: spreadView.slotWidth
            height: spreadView.height

            Image {
                anchors.centerIn: parent
                width: spreadView.slotSize.width
                height: spreadView.slotSize.height
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: true
                smooth: true
                visible: spreadView.hasLeft && spreadView.slotSize.width > 0
                source: spreadView.hasLeft
                        ? "image://comicpage/" + spreadView.leftIndex : ""
            }
        }

        // 右槽位
        Item {
            width: spreadView.hasLeft ? spreadView.slotWidth : 0
            height: spreadView.height

            Image {
                anchors.centerIn: parent
                width: spreadView.slotSize.width
                height: spreadView.slotSize.height
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: true
                smooth: true
                visible: spreadView.hasRight && spreadView.slotSize.width > 0
                source: spreadView.hasRight
                        ? "image://comicpage/" + spreadView.rightIndex : ""
            }
        }
    }

    Keys.onPressed: function (event) {
        if (!spreadView.controller || spreadView.controller.pageCount <= 0) {
            event.accepted = false
            return
        }
        if (event.key === Qt.Key_Right) {
            // 日漫右起：视觉上"下一页"应向左推进
            spreadView.controller.goToPage(
                spreadView.rightToLeft
                    ? spreadView.spreadStartIndex + 1
                    : spreadView.spreadStartIndex - 2)
            event.accepted = true
        } else if (event.key === Qt.Key_Left) {
            spreadView.controller.goToPage(
                spreadView.rightToLeft
                    ? spreadView.spreadStartIndex - 1
                    : spreadView.spreadStartIndex + 2)
            event.accepted = true
        } else {
            event.accepted = false
        }
    }
    // 焦点默认会被工具栏按钮抢走，导致键盘翻页失效，故显式夺取焦点
    focus: true
    Component.onCompleted: forceActiveFocus()
}

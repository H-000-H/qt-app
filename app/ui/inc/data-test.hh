#include "data-view.hh"
#include <qcontainerfwd.h>
namespace app
{
    struct SensorRecord
    {
        QString timestamp;
        double temp;
        double hum;
        QString status;

        //  实例方法：取第col列的数据 → QVariant
        QVariant chart_column(int col) const
        {
            switch(col)
            {
                case 0: return timestamp;
                case 1: return temp;
                case 2: return hum;
                case 3: return status;
                default: return {};
            }
        }
        //  静态：获取第col列表头名
        static QString chart_header(int col)
        {
            switch(col)
            {
                case 0: return "时间戳";
                case 1: return "温度";
                case 2: return "湿度";
                case 3: return "状态";
                default: return "";
            }
        }
        // 静态常量：总列数
        static constexpr int chart_column_count = 4;
    };

    static_assert(ChartElement<SensorRecord>);

}

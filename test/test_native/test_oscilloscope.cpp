#include <gtest/gtest.h>

extern "C" {
#include "app/oscilloscope/oscilloscope_engine.h"
}

TEST(OscilloscopeTrigger, RisingEdgeFound)
{
    uint16_t buf[] = {100, 100, 200, 400, 800, 400, 200};
    uint16_t idx = scope_find_trigger_rising(buf, 7, 0, 300);
    EXPECT_EQ(idx, 3);
}

TEST(OscilloscopeTrigger, NoEdgeReturnsInvalid)
{
    uint16_t buf[] = {100, 100, 100, 100};
    uint16_t idx = scope_find_trigger_rising(buf, 4, 0, 300);
    EXPECT_EQ(idx, 0xFFFF);
}

TEST(OscilloscopeTrigger, StartOffsetSkipsEarlierEdges)
{
    uint16_t buf[] = {100, 400, 100, 400, 800};
    uint16_t idx = scope_find_trigger_rising(buf, 5, 2, 300);
    EXPECT_EQ(idx, 3);
}

TEST(OscilloscopeTrigger, NullBufferReturnsInvalid)
{
    uint16_t idx = scope_find_trigger_rising(NULL, 0, 0, 300);
    EXPECT_EQ(idx, 0xFFFF);
}

TEST(OscilloscopeTrigger, LevelZeroTriggersOnRisingFromZero)
{
    uint16_t buf[] = {0, 0, 100, 200, 300};
    uint16_t idx = scope_find_trigger_rising(buf, 5, 0, 0);
    EXPECT_EQ(idx, 2);
}

TEST(OscilloscopeTrigger, LevelMaxDoesNotFalseTrigger)
{
    uint16_t buf[] = {100, 200, 300, 400, 500};
    uint16_t idx = scope_find_trigger_rising(buf, 5, 0, 4095);
    EXPECT_EQ(idx, 0xFFFF);
}

TEST(OscilloscopeTrigger, EqualLevelDoesNotTrigger)
{
    uint16_t buf[] = {100, 200, 300, 300, 400};
    uint16_t idx = scope_find_trigger_rising(buf, 5, 0, 300);
    EXPECT_EQ(idx, 4);
}

TEST(OscilloscopeParamTables, TimeBaseCountMatchesArray)
{
    EXPECT_EQ(SCOPE_TIME_BASE_COUNT, 7U);
    EXPECT_STREQ(g_scope_time_bases[0].label, "50us");
    EXPECT_GT(g_scope_time_bases[0].time_per_div_us, 0U);
    EXPECT_LT(g_scope_time_bases[0].time_per_div_us,
              g_scope_time_bases[SCOPE_TIME_BASE_COUNT - 1].time_per_div_us);
}

TEST(OscilloscopeParamTables, VoltRangeFullScaleOrdered)
{
    EXPECT_EQ(SCOPE_VOLT_RANGE_COUNT, 4U);
    for (uint8_t i = 1; i < SCOPE_VOLT_RANGE_COUNT; i++) {
        EXPECT_GE(g_scope_volt_ranges[i].full_scale,
                  g_scope_volt_ranges[i - 1].full_scale);
    }
    EXPECT_EQ(g_scope_volt_ranges[SCOPE_VOLT_RANGE_COUNT - 1].full_scale, 4095U);
}

TEST(OscilloscopeParamTables, LabelsNonNull)
{
    for (uint8_t i = 0; i < SCOPE_COUPLING_COUNT; i++) {
        EXPECT_NE(g_scope_coupling_labels[i], nullptr);
    }
    for (uint8_t i = 0; i < SCOPE_TRIGGER_MODE_COUNT; i++) {
        EXPECT_NE(g_scope_trigger_mode_labels[i], nullptr);
    }
}

TEST(OscilloscopeParamTables, SampleRateCountAndOrder)
{
    EXPECT_EQ(SCOPE_SAMPLE_RATE_COUNT, 6U);
    for (uint8_t i = 1; i < SCOPE_SAMPLE_RATE_COUNT; i++) {
        EXPECT_GT(g_scope_sample_rates[i].rate_hz, g_scope_sample_rates[i - 1].rate_hz);
    }
    EXPECT_EQ(g_scope_sample_rates[SCOPE_SAMPLE_RATE_COUNT - 1].rate_hz, 100000U);
}

TEST(OscilloscopeParamTables, FilterCountAndWeights)
{
    EXPECT_EQ(SCOPE_FILTER_COUNT, 4U);
    EXPECT_STREQ(g_scope_filters[0].label, "Off");
    EXPECT_EQ(g_scope_filters[0].prev_weight, 0U);
    EXPECT_LE(g_scope_filters[SCOPE_FILTER_COUNT - 1].prev_weight, 8U);
}

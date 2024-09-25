//@version=5
strategy("Optimized Autonomous Trading Strategy", overlay=true, default_qty_type=strategy.percent_of_equity, default_qty_value=1, pyramiding=2, initial_capital=10000, currency=currency.USD, calc_on_order_fills=true, calc_on_every_tick=true)

// =========================
// INPUT PARAMETERS
// =========================

// Risk Management Inputs
riskPerTradePercent = input.float(2.0, title="Risk Per Trade (%)", minval=0.1, maxval=10, step=0.1)
dailyLossLimitPercent = input.float(5.0, title="Daily Loss Limit (%)", minval=1.0, maxval=20.0, step=0.1)

// Trade Parameters
takeProfitMultiplierWeak = input.float(1.0, title="Take Profit Multiplier (Weak)", minval=0.1, step=0.1)
stopLossMultiplierWeak = input.float(0.5, title="Stop Loss Multiplier (Weak)", minval=0.1, step=0.1)
takeProfitMultiplierStrong = input.float(2.5, title="Take Profit Multiplier (Strong)", minval=0.1, step=0.1)
stopLossMultiplierStrong = input.float(1.1, title="Stop Loss Multiplier (Strong)", minval=0.1, step=0.1)
trailingStopPercent = input.float(50, title="Trailing Stop Percentage", minval=0.1, step=0.1)
maxConcurrentTrades = input.int(1, title="Maximum Concurrent Trades", minval=1, maxval=10)

// Indicator Parameters
rsiPeriod = input.int(14, title="RSI Period", minval=1)
rsiOverbought = input.int(70, title="RSI Overbought Level", minval=50, maxval=100)
rsiOversold = input.int(30, title="RSI Oversold Level", minval=0, maxval=50)

emaShortPeriod = input.int(14, title="EMA Short Period", minval=1)
emaLongPeriod = input.int(21, title="EMA Long Period", minval=1)

macdFastLength = input.int(12, title="MACD Fast Length", minval=1)
macdSlowLength = input.int(26, title="MACD Slow Length", minval=1)
macdSignalLength = input.int(8, title="MACD Signal Length", minval=1)

atrPeriod = input.int(14, title="ATR Period", minval=1)

bbLength = input.int(20, title="Bollinger Bands Length", minval=1)
bbStdDev = input.float(2.0, title="Bollinger Bands StdDev", minval=0.1, step=0.1)

stochKLength = input.int(20, title="Stochastic %K Length", minval=1)
stochDLength = input.int(3, title="Stochastic %D Length", minval=1)
stochOverbought = input.int(80, title="Stochastic Overbought Level", minval=50, maxval=100)
stochOversold = input.int(20, title="Stochastic Oversold Level", minval=0, maxval=50)

htfMinutes = input.timeframe("240", title="Higher Timeframe (in Minutes)")

// Scoring System Inputs
baseWeakThreshold = input.float(5.0, title="Base Weak Condition Threshold", minval=0.1, step=0.1)
baseStrongThreshold = input.float(10.0, title="Base Strong Condition Threshold", minval=0.1, step=0.1)

// Indicator Weights
baseWeightCandlestick = input.float(1.0, title="Base Weight - Candlestick Patterns", minval=0.1, step=0.1)
baseWeightRSI = input.float(0.5, title="Base Weight - RSI", minval=0.1, step=0.1)
baseWeightMACD = input.float(0.6, title="Base Weight - MACD", minval=0.1, step=0.1)
baseWeightTrend = input.float(0.7, title="Base Weight - Trend", minval=0.1, step=0.1)
baseWeightStochastic = input.float(0.5, title="Base Weight - Stochastic", minval=0.1, step=0.1)
baseWeightVWAP = input.float(0.5, title="Base Weight - VWAP", minval=0.1, step=0.1)
baseWeightHTF = input.float(0.7, title="Base Weight - Higher Timeframe", minval=0.1, step=0.1)

// Trading Hours
tradeStartHour = input.int(9, title="Trade Start Hour (24h)", minval=0, maxval=23)
tradeEndHour = input.int(16, title="Trade End Hour (24h)", minval=0, maxval=23)

// =========================
// VARIABLES
// =========================

// Daily Loss Tracking
var float dailyLoss = 0.0
var int lastTradeCount = 0

// Reset dailyLoss at the start of each day
if ta.change(time("D"))
    dailyLoss := 0.0
    lastTradeCount := strategy.closedtrades

// Calculate daily loss based on closed trades
if (strategy.closedtrades > lastTradeCount)
    for i = lastTradeCount to strategy.closedtrades - 1
        dailyLoss += strategy.closedtrades.profit(i)
    lastTradeCount := strategy.closedtrades

// Halt Trading if Daily Loss Limit is Hit
haltOnDailyLoss = dailyLoss <= -strategy.equity * (dailyLossLimitPercent / 100)

// =========================
// FUNCTIONS
// =========================

// Calculates position size based on risk per trade and ATR.
calculatePositionSize() =>
    riskAmount = strategy.equity * (riskPerTradePercent / 100)
    atrValue = ta.atr(atrPeriod)
    positionSize = riskAmount / atrValue
    positionSize

// Aggregates higher timeframe indicators.
getHTFIndicators() =>
    [rsiHTF, emaHTF] = request.security(syminfo.tickerid, htfMinutes, [ta.rsi(close, rsiPeriod), ta.ema(close, emaShortPeriod)])
    [rsiHTF, emaHTF]

// Computes the weighted score for long and short conditions.
computeScores(rsi, macdLine, signalLine, emaShort, emaLong, stochK, vwapCalc, rsiHTF, emaHTF, bullishEngulfing, bearishEngulfing, morningStar, eveningStar, macdBullish, macdBearish, trendBullish, trendBearish) =>
    // Initialize scores
    float longScore = 0.0
    float shortScore = 0.0

    // Candlestick Patterns
    if bullishEngulfing or morningStar
        longScore += baseWeightCandlestick
    if bearishEngulfing or eveningStar
        shortScore += baseWeightCandlestick

    // RSI
    if (rsi < rsiOversold)
        longScore += baseWeightRSI * (rsiOversold - rsi) / rsiOversold
    if (rsi > rsiOverbought)
        shortScore += baseWeightRSI * (rsi - rsiOverbought) / (100 - rsiOverbought)

    // MACD
    if (macdBullish)
        longScore += baseWeightMACD * (macdLine - signalLine) / macdLine
    if (macdBearish)
        shortScore += baseWeightMACD * (signalLine - macdLine) / signalLine

    // Trend
    if (trendBullish)
        longScore += baseWeightTrend * (emaShort - emaLong) / emaLong
    if (trendBearish)
        shortScore += baseWeightTrend * (emaLong - emaShort) / emaShort

    // Stochastic
    if (stochK < stochOversold)
        longScore += baseWeightStochastic * (stochOversold - stochK) / stochOversold
    if (stochK > stochOverbought)
        shortScore += baseWeightStochastic * (stochK - stochOverbought) / (100 - stochOverbought)

    // VWAP
    if (close > vwapCalc)
        longScore += baseWeightVWAP * (close - vwapCalc) / vwapCalc
    if (close < vwapCalc)
        shortScore += baseWeightVWAP * (vwapCalc - close) / close

    // Higher Timeframe Indicators
    if (rsiHTF < rsiOversold)
        longScore += baseWeightHTF * (rsiOversold - rsiHTF) / rsiOversold
    if (rsiHTF > rsiOverbought)
        shortScore += baseWeightHTF * (rsiHTF - rsiOverbought) / (100 - rsiOverbought)

    if (emaHTF > ta.ema(close, emaShortPeriod)[1])
        longScore += baseWeightHTF * (emaHTF - ta.ema(close, emaShortPeriod)[1]) / ta.ema(close, emaShortPeriod)[1]
    if (emaHTF < ta.ema(close, emaShortPeriod)[1])
        shortScore += baseWeightHTF * (ta.ema(close, emaShortPeriod)[1] - emaHTF) / emaHTF

    [longScore, shortScore]

// =========================
// INDICATORS
// =========================

// Core Indicators
rsi = ta.rsi(close, rsiPeriod)
emaShort = ta.ema(close, emaShortPeriod)
emaLong = ta.ema(close, emaLongPeriod)
[macdLine, signalLine, _] = ta.macd(close, macdFastLength, macdSlowLength, macdSignalLength)
atr = ta.atr(atrPeriod)
basis = ta.sma(close, bbLength)
dev = bbStdDev * ta.stdev(close, bbLength)
upperBB = basis + dev
lowerBB = basis - dev
stochK = ta.stoch(close, high, low, stochKLength)
stochD = ta.sma(stochK, stochDLength)
vwapCalc = ta.vwap(close)

// Higher Timeframe Indicators
[rsiHTF, emaHTF] = getHTFIndicators()

// Candlestick Patterns
isBullish = close > open
isBearish = close < open
morningStar = isBearish[2] and isBearish[1] and isBullish and (close > high[1])
eveningStar = isBullish[2] and isBullish[1] and isBearish and (close < low[1])
bullishEngulfing = isBearish[1] and isBullish and (close > open[1]) and (open < close[1])
bearishEngulfing = isBullish[1] and isBearish and (close < open[1]) and (open > close[1])

// MACD and Trend Conditions
macdBullish = macdLine > signalLine
macdBearish = macdLine < signalLine
trendBullish = emaShort > emaLong
trendBearish = emaShort < emaLong

// =========================
// SCORING SYSTEM
// =========================

// Dynamic Thresholds based on ATR
weakThreshold = baseWeakThreshold * atr
strongThreshold = baseStrongThreshold * atr

// Dynamic Weights based on recent performance (example: last 10 bars)
recentPerformanceFactor = ta.sma(close, 10) / close
weightCandlestick = baseWeightCandlestick * recentPerformanceFactor
weightRSI = baseWeightRSI * recentPerformanceFactor
weightMACD = baseWeightMACD * recentPerformanceFactor
weightTrend = baseWeightTrend * recentPerformanceFactor
weightStochastic = baseWeightStochastic * recentPerformanceFactor
weightVWAP = baseWeightVWAP * recentPerformanceFactor
weightHTF = baseWeightHTF * recentPerformanceFactor

[longScore, shortScore] = computeScores(rsi, macdLine, signalLine, emaShort, emaLong, stochK, vwapCalc, rsiHTF, emaHTF, bullishEngulfing, bearishEngulfing, morningStar, eveningStar, macdBullish, macdBearish, trendBullish, trendBearish)

// Define Conditions
longCondition = longScore >= weakThreshold
shortCondition = shortScore >= weakThreshold

strongLongCondition = longScore >= strongThreshold
strongShortCondition = shortScore >= strongThreshold

// =========================
// TRADE EXECUTION
// =========================

// Calculate position size
positionSize = calculatePositionSize()

// Entry Conditions
canTradeTime = (hour(time) >= tradeStartHour) and (hour(time) <= tradeEndHour)

// Execute Trades
if (not haltOnDailyLoss) and canTradeTime
    // Long Entries
    if (strongLongCondition and strategy.opentrades < maxConcurrentTrades)
        strategy.entry("Strong Long", strategy.long, qty=positionSize)
        strategy.exit("Strong Long_TP_SL", from_entry="Strong Long", limit=close + (atr * takeProfitMultiplierStrong), stop=close - (atr * stopLossMultiplierStrong), trail_points=atr * trailingStopPercent)
        alert("Strong Long position entered", alert.freq_once_per_bar)
    else if (longCondition and strategy.opentrades < maxConcurrentTrades)
        strategy.entry("Weak Long", strategy.long, qty=positionSize)
        strategy.exit("Weak Long_TP_SL", from_entry="Weak Long", limit=close + (atr * takeProfitMultiplierWeak), stop=close - (atr * stopLossMultiplierWeak), trail_points=atr * trailingStopPercent)
        alert("Weak Long position entered", alert.freq_once_per_bar)
        
    // Short Entries
    if (strongShortCondition and strategy.opentrades < maxConcurrentTrades)
        strategy.entry("Strong Short", strategy.short, qty=positionSize)
        strategy.exit("Strong Short_TP_SL", from_entry="Strong Short", limit=close - (atr * takeProfitMultiplierStrong), stop=close + (atr * stopLossMultiplierStrong), trail_points=atr * trailingStopPercent)
        alert("Strong Short position entered", alert.freq_once_per_bar)
    else if (shortCondition and strategy.opentrades < maxConcurrentTrades)
        strategy.entry("Weak Short", strategy.short, qty=positionSize)
        strategy.exit("Weak Short_TP_SL", from_entry="Weak Short", limit=close - (atr * takeProfitMultiplierWeak), stop=close + (atr * stopLossMultiplierWeak), trail_points=atr * trailingStopPercent)
        alert("Weak Short position entered", alert.freq_once_per_bar)

// Halt trading and close all positions if daily loss limit is hit
if (haltOnDailyLoss)
    strategy.close_all(comment="Daily Loss Limit Hit")
    alert("Daily loss limit reached. Trading halted for today.", alert.freq_once_per_bar)

// =========================
// PLOTTING
// =========================

plot(emaShort, title="EMA Short", color=color.orange, linewidth=1)
plot(emaLong, title="EMA Long", color=color.blue, linewidth=1)
plot(upperBB, title="Upper Bollinger Band", color=color.gray, linewidth=1, style=plot.style_linebr)
plot(lowerBB, title="Lower Bollinger Band", color=color.gray, linewidth=1, style=plot.style_linebr)

// Plot Buy/Sell Signals
plotshape(series=longCondition and strategy.opentrades < maxConcurrentTrades and not haltOnDailyLoss, title="Weak Buy", location=location.belowbar, color=color.green, style=shape.labelup, text="Weak Buy")
plotshape(series=shortCondition and strategy.opentrades < maxConcurrentTrades and not haltOnDailyLoss, title="Weak Sell", location=location.abovebar, color=color.red, style=shape.labeldown, text="Weak Sell")
plotshape(series=strongLongCondition and strategy.opentrades < maxConcurrentTrades and not haltOnDailyLoss, title="Strong Buy", location=location.belowbar, color=color.lime, style=shape.labelup, text="Strong Buy")
plotshape(series=strongShortCondition and strategy.opentrades < maxConcurrentTrades and not haltOnDailyLoss, title="Strong Sell", location=location.abovebar, color=color.maroon, style=shape.labeldown, text="Strong Sell")

// =========================
// TABLE DISPLAY
// =========================

// Create a table
var table indicatorTable = table.new(position.top_right, 6, 6, border_width=1)

// Update the table with current values
if (bar_index == 0)
    table.cell(indicatorTable, 0, 0, "Long Score", bgcolor=color.gray)
    table.cell(indicatorTable, 1, 0, "Short Score", bgcolor=color.gray)
    table.cell(indicatorTable, 2, 0, "Weak Threshold", bgcolor=color.gray)
    table.cell(indicatorTable, 3, 0, "Strong Threshold", bgcolor=color.gray)
    table.cell(indicatorTable, 4, 0, "Daily Loss", bgcolor=color.gray)
    table.cell(indicatorTable, 5, 0, "Limit", bgcolor=color.gray)

table.cell(indicatorTable, 0, 1, str.tostring(longScore, format.mintick))
table.cell(indicatorTable, 1, 1, str.tostring(shortScore, format.mintick))
table.cell(indicatorTable, 2, 1, str.tostring(weakThreshold, format.mintick))
table.cell(indicatorTable, 3, 1, str.tostring(strongThreshold, format.mintick))
table.cell(indicatorTable, 4, 1, str.tostring(dailyLoss, format.mintick))
table.cell(indicatorTable, 5, 1, str.tostring(dailyLossLimitPercent, format.mintick))

// =========================
// END OF SCRIPT
// =========================

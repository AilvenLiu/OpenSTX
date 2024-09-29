//@version=5
strategy("Advanced SPY 3-Min Strategy", overlay=true, 
     default_qty_type=strategy.percent_of_equity, 
     default_qty_value=1, 
     pyramiding=0, 
     initial_capital=10000, 
     currency=currency.USD, 
     commission_type=strategy.commission.percent, 
     commission_value=0.05)

// =========================
// INPUT PARAMETERS
// =========================

// Risk Management
riskPerTradePercent = input.float(0.2, "Risk Per Trade (%)", 0.1, 1.0, 0.1)
maxDailyLossPercent = input.float(1.0, "Max Daily Loss (%)", 0.5, 3.0, 0.1)
maxDailyTrades = input.int(10, "Max Daily Trades", 5, 20)

// Moving Average Parameters
fastEMA_Length = input.int(5, "Fast EMA Length", 3, 10)
slowEMA_Length = input.int(15, "Slow EMA Length", 10, 30)

// RSI Parameters
rsiLength = input.int(7, "RSI Length", 5, 14)
rsiOverbought = input.int(70, "RSI Overbought", 65, 80)
rsiOversold = input.int(30, "RSI Oversold", 20, 35)

// MACD Parameters
macdFastLength = input.int(12, "MACD Fast Length", 8, 20)
macdSlowLength = input.int(26, "MACD Slow Length", 20, 40)
macdSignalLength = input.int(9, "MACD Signal Length", 5, 15)

// Bollinger Bands Parameters
bbLength = input.int(20, "BB Length", 10, 30)
bbMult = input.float(2.0, "BB StdDev", 1.5, 3.0, 0.1)

// ATR for Volatility
atrPeriod = input.int(10, "ATR Period", 5, 20)
atrMultiplier = input.float(1.2, "ATR Multiplier for SL", 1.0, 2.0, 0.1)

// Volume Filter
volumeMA_Length = input.int(20, "Volume MA Length", 10, 50)

// Trading Hours
tradeStartHour = input.int(9, "Trade Start Hour (24h)", 0, 23)
tradeEndHour = input.int(16, "Trade End Hour (24h)", 0, 23)

// =========================
// INDICATOR CALCULATIONS
// =========================

fastEMA = ta.ema(close, fastEMA_Length)
slowEMA = ta.ema(close, slowEMA_Length)
rsi = ta.rsi(close, rsiLength)
[macdLine, signalLine, _] = ta.macd(close, macdFastLength, macdSlowLength, macdSignalLength)
[bbMiddle, bbUpper, bbLower] = ta.bb(close, bbLength, bbMult)
atr = ta.atr(atrPeriod)
volumeMA = ta.sma(volume, volumeMA_Length)

// =========================
// TRADING CONDITIONS
// =========================

currentHour = hour(time)
canTrade = (currentHour >= tradeStartHour) and (currentHour < tradeEndHour)

uptrend = fastEMA > slowEMA and close > slowEMA
downtrend = fastEMA < slowEMA and close < slowEMA

volumeFilter = volume > volumeMA
macdCrossUp = ta.crossover(macdLine, signalLine)
macdCrossDown = ta.crossunder(macdLine, signalLine)
bbCompressionRatio = (bbUpper - bbLower) / bbMiddle
isVolatile = bbCompressionRatio > 0.03  // Adjust this threshold as needed

longCondition = uptrend and rsi < rsiOversold and macdCrossUp and close < bbLower and volumeFilter and isVolatile and canTrade
shortCondition = downtrend and rsi > rsiOverbought and macdCrossDown and close > bbUpper and volumeFilter and isVolatile and canTrade

// =========================
// RISK MANAGEMENT
// =========================

var float dailyLoss = 0.0
var int tradeCount = 0

if ta.change(time("D"))
    dailyLoss := 0.0
    tradeCount := 0

stopTrading = dailyLoss <= -(strategy.equity * maxDailyLossPercent / 100) or tradeCount >= maxDailyTrades

calcPositionSize() =>
    riskAmount = strategy.equity * (riskPerTradePercent / 100)
    stopSize = atr * atrMultiplier
    positionSize = riskAmount / stopSize
    math.round(positionSize)

// =========================
// TRADE EXECUTION
// =========================

if not stopTrading
    if (longCondition)
        qty = calcPositionSize()
        strategy.entry("Long", strategy.long, qty=qty)
        strategy.exit("Exit Long", "Long", stop=strategy.position_avg_price - (atr * atrMultiplier), 
                      limit=strategy.position_avg_price + (atr * atrMultiplier * 2), 
                      trail_points=atr * atrMultiplier, trail_offset=atr * atrMultiplier)
    
    if (shortCondition)
        qty = calcPositionSize()
        strategy.entry("Short", strategy.short, qty=qty)
        strategy.exit("Exit Short", "Short", stop=strategy.position_avg_price + (atr * atrMultiplier), 
                      limit=strategy.position_avg_price - (atr * atrMultiplier * 2), 
                      trail_points=atr * atrMultiplier, trail_offset=atr * atrMultiplier)

if (strategy.closedtrades > 0)
    lastTrade = strategy.closedtrades.exit_bar_index(strategy.closedtrades - 1)
    if (lastTrade == bar_index)
        dailyLoss += strategy.closedtrades.profit(strategy.closedtrades - 1)
        tradeCount += 1

if (stopTrading)
    strategy.close_all("Daily Loss Limit or Max Trades Reached")

// =========================
// PLOTTING
// =========================

plot(fastEMA, color=color.blue, title="Fast EMA")
plot(slowEMA, color=color.red, title="Slow EMA")
plot(bbUpper, color=color.green, title="BB Upper")
plot(bbLower, color=color.green, title="BB Lower")

plotshape(longCondition, title="Long", location=location.belowbar, color=color.green, style=shape.triangleup, size=size.small)
plotshape(shortCondition, title="Short", location=location.abovebar, color=color.red, style=shape.triangledown, size=size.small)

// =========================
// ALERTS
// =========================

alertcondition(longCondition, title="Long Entry", message="Long entry signal")
alertcondition(shortCondition, title="Short Entry", message="Short entry signal")
alertcondition(stopTrading, title="Trading Halted", message="Daily loss limit or max trades reached")

// Create a table for key metrics
var table metricsTable = table.new(position.top_right, 2, 7, border_width=1, frame_color=color.black, frame_width=1)

// Initialize table headers
if (barstate.isfirst)
    table.cell(metricsTable, 0, 0, "Metric", bgcolor=color.gray)
    table.cell(metricsTable, 1, 0, "Value", bgcolor=color.gray)

// Update table with metrics
table.cell(metricsTable, 0, 1, "Fast EMA", bgcolor=color.white)
table.cell(metricsTable, 1, 1, str.tostring(fastEMA, format.mintick), bgcolor=color.white)
table.cell(metricsTable, 0, 2, "Slow EMA", bgcolor=color.white)
table.cell(metricsTable, 1, 2, str.tostring(slowEMA, format.mintick), bgcolor=color.white)
table.cell(metricsTable, 0, 3, "RSI", bgcolor=color.white)
table.cell(metricsTable, 1, 3, str.tostring(rsi, format.mintick), bgcolor=color.white)
table.cell(metricsTable, 0, 4, "ATR", bgcolor=color.white)
table.cell(metricsTable, 1, 4, str.tostring(atr, format.mintick), bgcolor=color.white)
table.cell(metricsTable, 0, 5, "Daily Loss", bgcolor=color.white)
table.cell(metricsTable, 1, 5, str.tostring(dailyLoss, format.percent), bgcolor=color.white)
table.cell(metricsTable, 0, 6, "Trade Count", bgcolor=color.white)
table.cell(metricsTable, 1, 6, str.tostring(tradeCount), bgcolor=color.white)
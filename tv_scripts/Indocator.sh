//@version=5
indicator("Indicator Plotter", overlay=false)

// =========================
// INPUT PARAMETERS
// =========================

rsiPeriod = input.int(14, title="RSI Period", minval=1)
rsiOverbought = input.int(70, title="RSI Overbought Level", minval=50, maxval=100)
rsiOversold = input.int(30, title="RSI Oversold Level", minval=0, maxval=50)

stochKLength = input.int(14, title="Stochastic %K Length", minval=1)
stochDLength = input.int(3, title="Stochastic %D Length", minval=1)
stochOverbought = input.int(80, title="Stochastic Overbought Level", minval=50, maxval=100)
stochOversold = input.int(20, title="Stochastic Oversold Level", minval=0, maxval=50)

// =========================
// INDICATORS
// =========================

rsi = ta.rsi(close, rsiPeriod)
stochK = ta.stoch(close, high, low, stochKLength)
stochD = ta.sma(stochK, stochDLength)
vwapCalc = ta.vwap(close)

// =========================
// PLOTTING
// =========================

hline(rsiOverbought, "RSI Overbought", color=color.red)
hline(rsiOversold, "RSI Oversold", color=color.green)
plot(rsi, title="RSI", color=color.blue)
plot(stochK, title="Stochastic %K", color=color.blue)
plot(stochD, title="Stochastic %D", color=color.orange)
plot(vwapCalc, title="VWAP", color=color.purple, linewidth=1)

// =========================
// END OF SCRIPT
// =========================
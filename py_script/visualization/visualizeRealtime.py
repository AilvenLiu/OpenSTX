import matplotlib.pyplot as plt
import matplotlib.dates as mdates
import pandas as pd
import numpy as np
from matplotlib.animation import FuncAnimation
from py_script.readSharedMemory import read_shared_memory
import datetime

# Initialize the figure and subplots
fig, (ax1, ax2, ax3) = plt.subplots(3, 1, figsize=(10, 8), gridspec_kw={'height_ratios': [2, 1, 1]})

# Initialize the data containers
ohlc_data = []
rsi_data = []
macd_data = []
depth_data = []

# Set up the format for dates on x-axis
ax1.xaxis.set_major_formatter(mdates.DateFormatter('%H:%M'))

# Shared memory settings
SHM_NAME = 'RealTimeData'
SHM_SIZE = 1024

def update(frame):
    global ohlc_data, rsi_data, macd_data, depth_data

    # Read data from shared memory
    df = read_shared_memory(SHM_NAME, SHM_SIZE)
    
    # Convert the 'Datetime' column to datetime objects
    df['Datetime'] = pd.to_datetime(df['Datetime'])
    
    # Update OHLC data
    ohlc_data = df[['Datetime', 'Open', 'High', 'Low', 'Close', 'Volume']].values
    
    # Update RSI and MACD (placeholder functions)
    df['RSI'] = calculate_rsi(df['Close'])
    df['MACD'] = calculate_macd(df['Close'])
    rsi_data = df[['Datetime', 'RSI']].values
    macd_data = df[['Datetime', 'MACD']].values

    # Update depth data
    depth_data = df[['BidPrice', 'BidSize', 'AskPrice', 'AskSize']].values

    # Clear previous data
    ax1.clear()
    ax2.clear()
    ax3.clear()

    # Plot OHLC (candlestick)
    ax1.plot(df['Datetime'], df['Close'], label='Close Price')
    ax1.fill_between(df['Datetime'], df['Low'], df['High'], color='gray', alpha=0.3)
    ax1.set_title('Real-Time OHLC Data')
    ax1.set_ylabel('Price')
    ax1.legend()

    # Plot RSI
    ax2.plot(df['Datetime'], df['RSI'], color='blue', label='RSI')
    ax2.axhline(70, color='red', linestyle='--')
    ax2.axhline(30, color='green', linestyle='--')
    ax2.set_title('RSI Indicator')
    ax2.set_ylabel('RSI')
    ax2.legend()

    # Plot MACD
    ax3.plot(df['Datetime'], df['MACD'], color='purple', label='MACD')
    ax3.set_title('MACD Indicator')
    ax3.set_ylabel('MACD')
    ax3.legend()

    # Rotate x-axis labels for all subplots
    for ax in [ax1, ax2, ax3]:
        plt.setp(ax.get_xticklabels(), rotation=45, ha='right')

    # Save the figure as a snapshot
    snapshot_filename = f'data/analysis_pic/realtime_snapshot_{datetime.datetime.now().strftime("%Y%m%d%H%M%S")}.png'
    plt.savefig(snapshot_filename)

# Placeholder functions for RSI and MACD
def calculate_rsi(close_prices, period=14):
    delta = close_prices.diff()
    gain = (delta.where(delta > 0, 0)).rolling(window=period).mean()
    loss = (-delta.where(delta < 0, 0)).rolling(window=period).mean()
    rs = gain / loss
    rsi = 100 - (100 / (1 + rs))
    return rsi

def calculate_macd(close_prices, short_period=12, long_period=26, signal_period=9):
    short_ema = close_prices.ewm(span=short_period, adjust=False).mean()
    long_ema = close_prices.ewm(span=long_period, adjust=False).mean()
    macd_line = short_ema - long_ema
    signal_line = macd_line.ewm(span=signal_period, adjust=False).mean()
    macd_histogram = macd_line - signal_line
    return macd_histogram

# Create animation
ani = FuncAnimation(fig, update, interval=60000)  # Update every minute

# Show the plot
plt.show()
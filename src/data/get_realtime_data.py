import pandas as pd
import os
from datetime import datetime
from ib_insync import IB, Stock  # Interactive Brokers API
import time
import pytz

# Connect to Interactive Brokers TWS
ib = IB()
ib.connect('127.0.0.1', 7496, clientId=2)

# Define data paths
realtime_original_data_path = 'data/realtime_original_data'
daily_realtime_data_path = 'data/daily_realtime_data'

# Create directories if they don't exist
os.makedirs(realtime_original_data_path, exist_ok=True)
os.makedirs(daily_realtime_data_path, exist_ok=True)

# Define contract for SPY
contract = Stock('SPY', 'SMART', 'USD')

# Define time zones
local_tz = pytz.timezone('Asia/Shanghai')  # Assuming device is in China
ny_tz = pytz.timezone('America/New_York')  # SPY trades in New York time zone

# Function to check if the market is open
def is_market_open():
    # Check if current time is within market hours (9:30 AM - 4:00 PM ET)
    # Get current time in local timezone
    now = datetime.now(local_tz)
    # Convert to New York time
    ny_now = now.astimezone(ny_tz)
    market_open_time = now.replace(hour=9, minute=30, second=0, microsecond=0)
    market_close_time = now.replace(hour=16, minute=0, second=0, microsecond=0)
    if market_open_time <= now <= market_close_time:
        return True
    print(f"Market is closed: {now}. Waiting for market to open...")

# Function to fetch L1 data
def fetch_l1_data():
    ticker = ib.reqMktData(contract, '', False, False)
    ib.sleep(0.5)  # Wait for data to be populated
    if ticker:
        data = {
            'Datetime': datetime.now(),
            'Bid': ticker.bid,
            'Ask': ticker.ask,
            'Last': ticker.last,
            'Open': ticker.open,
            'High': ticker.high,
            'Low': ticker.low,
            'Close': ticker.close,
            'Volume': ticker.volume
        }
        return data
    return None

# Function to fetch L2 data
def fetch_l2_data():
    market_depth = ib.reqMktDepth(contract)
    ib.sleep(0.5)  # Wait for data to be populated
    l2_data = []
    if market_depth:
        for level in market_depth:
            l2_data.append({
                'Datetime': datetime.now(),
                'Position': level.position,
                'MarketMaker': level.marketMaker,
                'Operation': level.operation,
                'Side': level.side,
                'Price': level.price,
                'Size': level.size
            })
    return l2_data

# Function to write data to CSV
def write_to_csv(data, file_path):
    df = pd.DataFrame(data)
    df.to_csv(file_path, mode='a', header=not os.path.exists(file_path), index=False)

# Real-time data collection loop
def collect_realtime_data():
    while True:
        if is_market_open():
            # Fetch L1 and L2 data
            l1_data = fetch_l1_data()
            l2_data = fetch_l2_data()

            # Write original L1 data to CSV
            if l1_data:
                original_l1_file_path = os.path.join(realtime_original_data_path, f"SPY_realtime_l1_{datetime.now().date()}.csv")
                write_to_csv([l1_data], original_l1_file_path)

            # Write original L2 data to CSV
            if l2_data:
                original_l2_file_path = os.path.join(realtime_original_data_path, f"SPY_realtime_l2_{datetime.now().date()}.csv")
                write_to_csv(l2_data, original_l2_file_path)

            # Perform feature engineering and write to daily data CSV
            if l1_data and l2_data:
                features = perform_feature_engineering(l1_data, l2_data)
                # Add L1 and L2 data to features
                combined_data = {**l1_data, **features}
                # Include more L2 details
                for i, l2 in enumerate(l2_data):
                    combined_data[f"L2_Position_{i}"] = l2['Position']
                    combined_data[f"L2_MarketMaker_{i}"] = l2['MarketMaker']
                    combined_data[f"L2_Operation_{i}"] = l2['Operation']
                    combined_data[f"L2_Side_{i}"] = l2['Side']
                    combined_data[f"L2_Price_{i}"] = l2['Price']
                    combined_data[f"L2_Size_{i}"] = l2['Size']

                # Ensure data is written to the same file throughout the day
                daily_feature_file_path = os.path.join(daily_realtime_data_path, f"SPY_realtime_{datetime.now().date()}.csv")
                write_to_csv([combined_data], daily_feature_file_path)

            print(f"Real-time data updated at {datetime.now()}")
            time.sleep(1)  # Sleep for 1 second before next fetch

        else:
            time.sleep(60)  # Sleep for 1 minute before checking again

# Feature engineering function for real-time data
def perform_feature_engineering(l1_data, l2_data):
    # Example of feature engineering
    features = {
        'Bid-Ask Spread': l1_data['Ask'] - l1_data['Bid'],
        'Midpoint': (l1_data['Bid'] + l1_data['Ask']) / 2,
        'Price Change': l1_data['Close'] - l1_data['Open'],
        'L1 Volume': l1_data['Volume'],
        # Calculate total L2 depth
        'L2 Depth': sum(item['Size'] for item in l2_data)
    }
    return features

# Start collecting real-time data
collect_realtime_data()
from ib_insync import *
import csv
from datetime import datetime

def execute_trades(predictions, symbols, data):
    ib = IB()
    ib.connect('127.0.0.1', 7497, clientId=1)
    
    trades = []
    for symbol, prediction in zip(symbols, predictions):
        contract = Stock(symbol, 'SMART', 'USD')
        ib.qualifyContracts(contract)
        
        current_price = data[data['symbol'] == symbol]['close'].iloc[-1]
        if prediction > current_price:
            order = MarketOrder('BUY', 10)
            trade = ib.placeOrder(contract, order)
            trades.append(trade)
            ib.sleep(1)
            print(f"Executed trade for {symbol}: {trade}")

    ib.disconnect()
    record_trades(trades)
    return trades

def record_trades(trades):
    with open('trading_log.csv', mode='a') as file:
        writer = csv.writer(file)
        for trade in trades:
            writer.writerow([datetime.now(), trade.contract.symbol, trade.order.action, trade.orderStatus.filled, trade.orderStatus.avgFillPrice])
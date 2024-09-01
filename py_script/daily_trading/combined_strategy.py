def combine_predictions(predictions_list):
    combined_predictions = sum(predictions_list) / len(predictions_list)
    return combined_predictions

if __name__ == "__main__":
    predictions_list = [
        [100, 200, 150, 300, 250, 400, 350, 450, 500, 550, 600, 650, 700, 750],
        [110, 210, 160, 310, 260, 410, 360, 460, 510, 560, 610, 660, 710, 760]
    ]
    combined_predictions = combine_predictions(predictions_list)
    print(combined_predictions)
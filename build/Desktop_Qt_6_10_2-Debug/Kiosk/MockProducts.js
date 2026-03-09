.pragma library

var PRODUCTS = [
  { barcode: "012345678905", name: "Milk 2%", brand: "H-E-B", size: "1 gal", category: "Dairy" },
  { barcode: "049000006346", name: "Coca-Cola", brand: "Coke", size: "12 oz", category: "Beverage" },
  { barcode: "038000138416", name: "Oreos", brand: "Oreo", size: "14.3 oz", category: "Snack" }
];

function lookup(barcode) {
  for (var i = 0; i < PRODUCTS.length; i++) {
    if (PRODUCTS[i].barcode === barcode)
      return PRODUCTS[i];
  }
  return null;
}

function randomBarcode() {
  var idx = Math.floor(Math.random() * PRODUCTS.length);
  return PRODUCTS[idx].barcode;
}

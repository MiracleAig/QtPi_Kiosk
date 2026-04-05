let inventory = [];
let selectedBarcode = null;

document.addEventListener("DOMContentLoaded", () => {
  document.getElementById("refreshBtn").addEventListener("click", refreshInventory);
  document.getElementById("searchInput").addEventListener("input", renderInventory);
  document.getElementById("addFoodForm").addEventListener("submit", handleAddFood);

  setupTheme();
  refreshInventory();
});

function setupTheme() {
  const toggle = document.getElementById("themeToggle");
  const saved = localStorage.getItem("fridgesense-theme") || "light";

  if (saved === "dark") {
    document.body.classList.add("dark");
    toggle.checked = true;
  }

  toggle.addEventListener("change", () => {
    const isDark = toggle.checked;
    document.body.classList.toggle("dark", isDark);
    localStorage.setItem("fridgesense-theme", isDark ? "dark" : "light");
  });
}

async function refreshInventory() {
  try {
    const response = await fetch("/api/inventory.json");
    if (!response.ok) throw new Error("Failed to load inventory");

    const data = await response.json();
    inventory = Array.isArray(data.inventory) ? data.inventory : [];

    renderInventory();

    if (selectedBarcode) {
      const selected = inventory.find(i => String(i.barcode || "") === String(selectedBarcode));
      if (selected) {
        showDetails(selected);
      } else {
        selectedBarcode = null;
        clearDetails();
      }
    }
  } catch (err) {
    console.error(err);
    clearDetails();
    renderInventory();
  }
}

function renderInventory() {
  const body = document.getElementById("inventoryBody");
  const emptyState = document.getElementById("emptyState");
  const search = document.getElementById("searchInput").value.trim().toLowerCase();

  body.innerHTML = "";

  const filtered = inventory.filter(item => {
    const name = String(item.name || "").toLowerCase();
    const brand = String(item.brand || "").toLowerCase();
    const barcode = String(item.barcode || "").toLowerCase();
    return name.includes(search) || brand.includes(search) || barcode.includes(search);
  });

  if (filtered.length === 0) {
    emptyState.classList.remove("hidden");
    return;
  }

  emptyState.classList.add("hidden");

  for (const item of filtered) {
    const tr = document.createElement("tr");
    tr.setAttribute("data-clickable", "true");
    tr.addEventListener("click", () => showDetails(item));

    const image = escapeAttr(item.image || "");
    const name = escapeHtml(item.name || "Unknown");
    const brand = escapeHtml(item.brand || "-");
    const barcode = escapeHtml(item.barcode || "-");

    tr.innerHTML = `
      <td>
        <img class="thumb" src="${image || "https://via.placeholder.com/52?text=No"}"
             alt="Food"
             onerror="this.src='https://via.placeholder.com/52?text=No'">
      </td>
      <td>${name}</td>
      <td>${brand}</td>
      <td>${barcode}</td>
      <td>
        <div class="inline-actions">
          <button class="btn btn-danger" onclick="event.stopPropagation(); deleteFood('${escapeJs(item.barcode || "")}')">
            Delete
          </button>
        </div>
      </td>
    `;

    body.appendChild(tr);
  }
}

function clearDetails() {
  document.getElementById("detailsContent").innerHTML =
    "Select a food item to view its picture and nutrition data.";
}

function showDetails(item) {
  selectedBarcode = item.barcode || null;

  const image = escapeAttr(item.image || "");
  const name = escapeHtml(item.name || "Unknown");
  const brand = escapeHtml(item.brand || "-");
  const barcode = escapeHtml(item.barcode || "-");
  const size = escapeHtml(item.size || "-");

  const calories = escapeHtml(String(getNutritionValue(item, ["calories", "energy_kcal", "energy-kcal_100g", "energy-kcal"])));
  const protein = escapeHtml(String(getNutritionValue(item, ["protein", "proteins", "proteins_100g", "protein_100g"])));
  const carbs = escapeHtml(String(getNutritionValue(item, ["carbs", "carbohydrates", "carbohydrates_100g", "carbs_100g"])));
  const fat = escapeHtml(String(getNutritionValue(item, ["fat", "fat_100g"])));
  const sugar = escapeHtml(String(getNutritionValue(item, ["sugar", "sugars", "sugars_100g", "sugar_100g"])));
  const sodium = escapeHtml(String(getNutritionValue(item, ["sodium", "sodium_100g", "salt", "salt_100g"])));

  document.getElementById("detailsContent").innerHTML = `
    <div class="details-card">
      <img class="details-image" src="${image || "https://via.placeholder.com/280?text=No+Image"}"
           alt="Food image"
           onerror="this.src='https://via.placeholder.com/280?text=No+Image'">

      <h3 class="details-title">${name}</h3>
      <p class="details-meta"><strong>Brand:</strong> ${brand}</p>
      <p class="details-meta"><strong>Barcode:</strong> ${barcode}</p>
      <p class="details-meta"><strong>Size:</strong> ${size}</p>

      <div class="nutrition-grid">
        <div class="nutrition-item">
          <div class="nutrition-label">Calories</div>
          <div class="nutrition-value">${calories}</div>
        </div>
        <div class="nutrition-item">
          <div class="nutrition-label">Protein (g)</div>
          <div class="nutrition-value">${protein}</div>
        </div>
        <div class="nutrition-item">
          <div class="nutrition-label">Carbs (g)</div>
          <div class="nutrition-value">${carbs}</div>
        </div>
        <div class="nutrition-item">
          <div class="nutrition-label">Fat (g)</div>
          <div class="nutrition-value">${fat}</div>
        </div>
        <div class="nutrition-item">
          <div class="nutrition-label">Sugar (g)</div>
          <div class="nutrition-value">${sugar}</div>
        </div>
        <div class="nutrition-item">
          <div class="nutrition-label">Sodium (mg)</div>
          <div class="nutrition-value">${sodium}</div>
        </div>
      </div>
    </div>
  `;
}

async function handleAddFood(event) {
  event.preventDefault();

  const payload = {
    barcode: document.getElementById("barcode").value.trim(),
    name: document.getElementById("name").value.trim(),
    brand: document.getElementById("brand").value.trim(),
    size: document.getElementById("size").value.trim(),
    image: document.getElementById("image").value.trim(),
    calories: parseNumberField("calories"),
    protein: parseNumberField("protein"),
    carbs: parseNumberField("carbs"),
    fat: parseNumberField("fat"),
    sugar: parseNumberField("sugar"),
    sodium: parseNumberField("sodium")
  };

  try {
    const response = await fetch("/api/add-food", {
      method: "POST",
      headers: {
        "Content-Type": "application/json"
      },
      body: JSON.stringify(payload)
    });

    const data = await response.json();

    if (!response.ok || !data.ok) {
      throw new Error(data.error || "Failed to add food");
    }

    document.getElementById("formMessage").textContent = "Food added successfully.";
    document.getElementById("addFoodForm").reset();
    await refreshInventory();
  } catch (err) {
    console.error(err);
    document.getElementById("formMessage").textContent = err.message;
  }
}

async function deleteFood(barcode) {
  if (!barcode) return;
  if (!confirm("Delete this food from inventory?")) return;

  try {
    const response = await fetch("/api/delete-food", {
      method: "POST",
      headers: {
        "Content-Type": "application/json"
      },
      body: JSON.stringify({ barcode })
    });

    const data = await response.json();

    if (!response.ok || !data.ok) {
      throw new Error(data.error || "Failed to delete food");
    }

    if (String(selectedBarcode) === String(barcode)) {
      selectedBarcode = null;
      clearDetails();
    }

    await refreshInventory();
  } catch (err) {
    console.error(err);
    alert(err.message);
  }
}

function getNutritionValue(item, keys) {
  const nutriments = item && typeof item.nutriments === "object" ? item.nutriments : null;

  for (const key of keys) {
    const value = item?.[key];
    if (value !== undefined && value !== null && value !== "") return value;

    if (nutriments && nutriments[key] !== undefined && nutriments[key] !== null && nutriments[key] !== "") {
      return nutriments[key];
    }
  }

  return "-";
}

function parseNumberField(id) {
  const value = document.getElementById(id).value.trim();
  if (value === "") return null;
  const n = Number(value);
  return Number.isFinite(n) ? n : null;
}

function escapeHtml(value) {
  return String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#39;");
}

function escapeAttr(value) {
  return escapeHtml(value);
}

function escapeJs(value) {
  return String(value)
    .replaceAll("\\", "\\\\")
    .replaceAll("'", "\\'");
}
